#include "../native_registry.hpp"
#include "../event_loop.hpp"
#include <httplib.h>
#include <filesystem>
#include <thread>
#include <future>
#include <sstream>
#include <vector>

namespace shell_lite {

static std::vector<std::string> split_path_segments(const std::string& path) {
    std::vector<std::string> segments;
    std::stringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (!seg.empty()) {
            segments.push_back(seg);
        }
    }
    return segments;
}

static bool match_route_pattern(const std::string& pattern, const std::string& actual_path,
                                std::unordered_map<std::string, std::string>& out_params) {
    if (pattern == actual_path) return true;
    auto pat_segs = split_path_segments(pattern);
    auto act_segs = split_path_segments(actual_path);
    if (pat_segs.size() != act_segs.size()) return false;

    for (size_t i = 0; i < pat_segs.size(); ++i) {
        if (!pat_segs[i].empty() && pat_segs[i][0] == ':') {
            std::string param_name = pat_segs[i].substr(1);
            out_params[param_name] = act_segs[i];
        } else if (pat_segs[i] == "*") {
        } else if (pat_segs[i] != act_segs[i]) {
            return false;
        }
    }
    return true;
}

static Value build_request_dict(VM* vm, const httplib::Request& req,
                                const std::unordered_map<std::string, std::string>& params) {
    auto* d = vm->arena().allocate<ObjDict>();
    GCRootGuard guard_d(vm->arena(), d);
    d->elements["method"] = Value(vm->arena().allocate_string(req.method));
    d->elements["path"] = Value(vm->arena().allocate_string(req.path));
    d->elements["body"] = Value(vm->arena().allocate_string(req.body));

    auto* p_dict = vm->arena().allocate<ObjDict>();
    GCRootGuard guard_p(vm->arena(), p_dict);
    for (const auto& pair : params) {
        p_dict->elements[pair.first] = Value(vm->arena().allocate_string(pair.second));
    }
    d->elements["params"] = Value(p_dict);

    auto* q_dict = vm->arena().allocate<ObjDict>();
    GCRootGuard guard_q(vm->arena(), q_dict);
    for (const auto& pair : req.params) {
        q_dict->elements[pair.first] = Value(vm->arena().allocate_string(pair.second));
    }
    d->elements["query"] = Value(q_dict);

    auto* h_dict = vm->arena().allocate<ObjDict>();
    GCRootGuard guard_h(vm->arena(), h_dict);
    for (const auto& pair : req.headers) {
        h_dict->elements[pair.first] = Value(vm->arena().allocate_string(pair.second));
    }
    d->elements["headers"] = Value(h_dict);

    return Value(d);
}

void register_stdlib_web(VM* vm) {
    NativeRegistry::register_builtin(vm, "std_web_listen", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("web_listen expects at least 1 argument (port)"));
            return Value();
        }
        int port = static_cast<int>(vm->peek(arg_count - 1).as_number());

        std::thread([vm, port]() {
            httplib::Server svr;

            svr.set_pre_routing_handler([vm](const httplib::Request& req, httplib::Response& res) {
                std::string method = req.method;
                std::string path = req.path;

                {
                    std::shared_lock<std::shared_mutex> r_lock(vm->web_mutex);
                    for (const auto& sr : vm->static_routes) {
                        if (path.find(sr.first) == 0) {
                            std::string rel_path = path.substr(sr.first.length());
                            if (rel_path.empty() || rel_path == "/") rel_path = "/index.html";
                            std::filesystem::path root_path = std::filesystem::weakly_canonical(sr.second);
                            std::filesystem::path full_path = (root_path / rel_path.substr(1)).lexically_normal();
                            std::string root_str = root_path.string();
                            std::string full_str = full_path.string();

                            if (full_str.rfind(root_str, 0) == 0 && std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
                                std::ifstream file(full_path, std::ios::binary);
                                if (file) {
                                    std::string ext = full_path.extension().string();
                                    std::string content_type = "text/plain";
                                    if (ext == ".html") content_type = "text/html";
                                    else if (ext == ".css") content_type = "text/css";
                                    else if (ext == ".js") content_type = "application/javascript";
                                    else if (ext == ".json") content_type = "application/json";
                                    else if (ext == ".png") content_type = "image/png";
                                    else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
                                    else if (ext == ".svg") content_type = "image/svg+xml";
                                    else if (ext == ".ico") content_type = "image/x-icon";

                                    std::string response_body = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                                    res.status = 200;
                                    res.set_content(response_body, content_type);
                                    return httplib::Server::HandlerResponse::Handled;
                                }
                            }
                        }
                    }
                }

                struct WebResponseData {
                    int status = 200;
                    std::string content_type = "text/plain";
                    std::string body;
                    std::vector<std::pair<std::string, std::string>> headers;
                };
                auto promise = std::make_shared<std::promise<WebResponseData>>();
                auto future = promise->get_future();

                vm->enqueue_task([vm, method, path, req, promise]() {
                    WebResponseData data;
                    data.status = 200;
                    data.content_type = "text/plain";

                    std::vector<Value> mw_list;
                    std::unordered_map<std::string, Value> routes_map;
                    {
                        std::shared_lock<std::shared_mutex> r_lock(vm->web_mutex);
                        mw_list = vm->web_middlewares;
                        routes_map = vm->web_routes;
                    }

                    for (auto& mw : mw_list) {
                        size_t mw_depth = vm->frames.size();
                        if (vm->call_value(mw, 0)) {
                            vm->run(static_cast<int>(mw_depth) - 1);
                        }
                    }

                    Value handler_val;
                    std::unordered_map<std::string, std::string> route_params;
                    std::string method_prefix = method + ":";

                    auto it = routes_map.find(method_prefix + path);
                    if (it != routes_map.end()) {
                        handler_val = it->second;
                    } else {
                        it = routes_map.find(path);
                        if (it != routes_map.end()) {
                            handler_val = it->second;
                        } else {
                            for (const auto& route_pair : routes_map) {
                                std::string registered_pattern = route_pair.first;
                                if (registered_pattern.rfind(method_prefix, 0) == 0) {
                                    std::string pat = registered_pattern.substr(method_prefix.length());
                                    if (match_route_pattern(pat, path, route_params)) {
                                        handler_val = route_pair.second;
                                        break;
                                    }
                                } else if (registered_pattern.find(':') == std::string::npos || registered_pattern[0] == '/') {
                                    if (match_route_pattern(registered_pattern, path, route_params)) {
                                        handler_val = route_pair.second;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (!handler_val.is_null()) {
                        Value req_val = build_request_dict(vm, req, route_params);
                        int arity = 0;
                        if (handler_val.is_closure()) {
                            arity = static_cast<ObjClosure*>(handler_val.get_obj())->function->arity;
                        } else if (handler_val.is_function()) {
                            arity = static_cast<ObjFunction*>(handler_val.get_obj())->arity;
                        } else {
                            arity = 1;
                        }

                        size_t depth = vm->frames.size();
                        if (arity > 0) {
                            vm->push(req_val);
                        }
                        if (vm->call_value(handler_val, arity > 0 ? 1 : 0)) {
                            Value v_res = vm->run(static_cast<int>(depth) - 1);
                            if (v_res.is_string()) {
                                data.body = v_res.as_string();
                                data.status = 200;
                                data.content_type = (data.body.rfind("<", 0) == 0) ? "text/html" : "text/plain";
                            } else if (v_res.is_dict()) {
                                auto* dict = static_cast<ObjDict*>(v_res.get_obj());
                                if (dict->elements.count("body")) {
                                    data.body = dict->elements["body"].to_string();
                                    data.status = dict->elements.count("status") ? static_cast<int>(dict->elements["status"].as_number()) : 200;
                                    data.content_type = dict->elements.count("content_type") ? dict->elements["content_type"].to_string() :
                                                        ((data.body.rfind("<", 0) == 0) ? "text/html" : "text/plain");
                                    if (dict->elements.count("headers") && dict->elements["headers"].is_dict()) {
                                        auto* h_d = static_cast<ObjDict*>(dict->elements["headers"].get_obj());
                                        for (const auto& h_pair : h_d->elements) {
                                            data.headers.push_back({h_pair.first, h_pair.second.to_string()});
                                        }
                                    }
                                } else {
                                    data.body = v_res.to_string();
                                    data.status = 200;
                                    data.content_type = "application/json";
                                }
                            } else if (v_res.is_null()) {
                                data.body = "";
                                data.status = 204;
                                data.content_type = "text/plain";
                            } else {
                                data.body = v_res.to_string();
                                data.status = 200;
                                data.content_type = "text/plain";
                            }
                        }
                    }
                    promise->set_value(data);
                });

                WebResponseData res_data = future.get();
                res.status = res_data.status;
                for (const auto& h : res_data.headers) {
                    res.set_header(h.first, h.second);
                }
                res.set_content(res_data.body, res_data.content_type);
                return httplib::Server::HandlerResponse::Handled;
            });

            svr.listen("0.0.0.0", port);
        }).detach();

        return Value(true);
    });

    NativeRegistry::register_builtin(vm, "std_web_start", -1, [](VM* vm, int arg_count) -> Value {
        Value v_listen = vm->globals->values["std_web_listen"];
        if (v_listen.is_callable()) {
            Callable* c = static_cast<Callable*>(v_listen.get_obj());
            vm->push(Value(8080.0));
            return c->call(vm, 1);
        }
        return Value(false);
    });

    NativeRegistry::register_builtin(vm, "std_web_serve_static", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 2) return Value(false);
        std::string route = vm->peek(1).to_string();
        std::string dir = vm->peek(0).to_string();
        {
            std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
            vm->static_routes[route] = dir;
        }
        return Value(true);
    });

    NativeRegistry::register_builtin(vm, "std_web_on_request", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count == 2) {
            std::string path = vm->peek(1).to_string();
            Value handler = vm->peek(0);
            {
                std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
                vm->web_routes[path] = handler;
            }
            return Value(true);
        }
        if (arg_count == 3) {
            std::string method = vm->peek(2).to_string();
            std::string path = vm->peek(1).to_string();
            Value handler = vm->peek(0);
            std::string key = method + ":" + path;
            {
                std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
                vm->web_routes[key] = handler;
                vm->web_routes[path] = handler;
            }
            return Value(true);
        }
        vm->has_error = true;
        vm->error_value = Value(vm->arena().allocate_string("web_on_request expects 2 or 3 arguments (method, path, handler)"));
        return Value();
    });

    NativeRegistry::register_builtin(vm, "serve_files_from", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 2) return Value(false);
        std::string route = vm->peek(1).to_string();
        std::string dir = vm->peek(0).to_string();
        {
            std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
            vm->static_routes[route] = dir;
        }
        return Value(true);
    });

    NativeRegistry::register_builtin(vm, "start_server", -1, [](VM* vm, int arg_count) -> Value {
        int port = (arg_count >= 1) ? static_cast<int>(vm->peek(0).as_number()) : 8080;
        vm->push(Value(static_cast<double>(port)));
        Value v_listen = vm->globals->values["std_web_listen"];
        if (v_listen.is_callable()) {
            Callable* c = static_cast<Callable*>(v_listen.get_obj());
            return c->call(vm, 1);
        }
        return Value();
    });

    NativeRegistry::register_builtin(vm, "add_middleware", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) return Value(false);
        {
            std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
            vm->web_middlewares.push_back(vm->peek(arg_count - 1));
        }
        return Value(true);
    });

    NativeRegistry::register_builtin(vm, "when_someone_visits", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 2) return Value();
        std::string route = vm->peek(1).to_string();
        Value handler = vm->peek(0);
        {
            std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
            vm->web_routes["GET:" + route] = handler;
            vm->web_routes[route] = handler;
        }
        return Value(true);
    });

    NativeRegistry::register_builtin(vm, "when_someone_submits", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 2) return Value();
        std::string route = vm->peek(1).to_string();
        Value handler = vm->peek(0);
        std::string key = "POST:" + route;
        {
            std::unique_lock<std::shared_mutex> w_lock(vm->web_mutex);
            vm->web_routes[key] = handler;
            vm->web_routes[route] = handler;
        }
        return Value(true);
    });
}

} // namespace shell_lite
