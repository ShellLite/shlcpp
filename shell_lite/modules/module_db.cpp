#include "../native_registry.hpp"
#include <sqlite3.h>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <cctype>

namespace shell_lite {

static std::mutex db_mutex;
thread_local ObjDatabase* t_active_db = nullptr;

static bool is_valid_sql_identifier(const std::string& id) {
    if (id.empty() || id.length() > 128) return false;
    if (!std::isalpha((unsigned char)id[0]) && id[0] != '_') return false;
    for (char c : id) {
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    }
    return true;
}

static std::string quote_identifier(const std::string& id) {
    std::string q = "\"";
    for (char c : id) {
        if (c == '"') q += "\"\"";
        else q += c;
    }
    q += "\"";
    return q;
}

static std::pair<std::string, std::vector<Value>> build_where_clause(const Value& conds, std::string& err_out) {
    if (!conds.is_dict()) return {"", {}};
    ObjDict* dict = static_cast<ObjDict*>(conds.get_obj());
    if (dict->elements.empty()) return {"", {}};
    
    std::string clause = " WHERE ";
    std::vector<Value> binds;
    bool first = true;
    for (auto& pair : dict->elements) {
        if (!is_valid_sql_identifier(pair.first)) {
            err_out = "Invalid column identifier in WHERE clause: " + pair.first;
            return {"", {}};
        }
        if (!first) clause += " AND ";
        clause += quote_identifier(pair.first) + " = ?";
        binds.push_back(pair.second);
        first = false;
    }
    return {clause, binds};
}

static sqlite3* resolve_db_conn(VM* vm, int arg_count) {
    for (int i = 0; i < arg_count; ++i) {
        Value v = vm->peek(i);
        if (v.is_database()) {
            ObjDatabase* db = v.as_database();
            if (db && db->conn && db->is_open) return db->conn;
        }
    }
    if (t_active_db && t_active_db->conn && t_active_db->is_open) {
        return t_active_db->conn;
    }
    return nullptr;
}

void register_stdlib_db(VM* vm) {
    NativeRegistry::register_builtin(vm, "std_db_open", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 1) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_open expects 1 argument"));
            return Value();
        }
        std::string path = vm->peek(0).to_string();
        auto* db_obj = vm->arena().allocate<ObjDatabase>(path);
        if (!db_obj || !db_obj->is_open) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Cannot open database: " + path));
            return Value();
        }
        t_active_db = db_obj;
        return Value(db_obj);
    });

    NativeRegistry::register_builtin(vm, "std_db_close", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count >= 1 && vm->peek(arg_count - 1).is_database()) {
            ObjDatabase* db = vm->peek(arg_count - 1).as_database();
            if (db && db->conn) {
                sqlite3_close(db->conn);
                db->conn = nullptr;
                db->is_open = false;
                if (t_active_db == db) t_active_db = nullptr;
            }
            return Value(true);
        }
        if (t_active_db && t_active_db->conn) {
            sqlite3_close(t_active_db->conn);
            t_active_db->conn = nullptr;
            t_active_db->is_open = false;
            t_active_db = nullptr;
            return Value(true);
        }
        return Value(false);
    });

    NativeRegistry::register_builtin(vm, "std_db_query_rows", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count < 1) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_query expects at least 1 argument"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string query = vm->peek(arg_count - 1).to_string();

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Failed to execute query: " + std::string(sqlite3_errmsg(conn))));
            return Value();
        }

        for (int i = 0; i < arg_count - 1; ++i) {
            Value v = vm->peek(arg_count - 2 - i);
            if (v.is_database()) continue;
            if (v.is_null()) sqlite3_bind_null(stmt, i + 1);
            else if (v.is_number()) sqlite3_bind_double(stmt, i + 1, v.as_number());
            else if (v.is_bool()) sqlite3_bind_int(stmt, i + 1, v.as_bool() ? 1 : 0);
            else sqlite3_bind_text(stmt, i + 1, v.to_string().c_str(), -1, SQLITE_TRANSIENT);
        }

        auto result_list = vm->arena().allocate<ObjList>();
        GCRootGuard res_guard(vm->arena(), result_list);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto row_dict = vm->arena().allocate<ObjDict>();
            result_list->elements.push_back(Value(row_dict));
            int cols = sqlite3_column_count(stmt);
            for (int i = 0; i < cols; ++i) {
                std::string col_name = sqlite3_column_name(stmt, i);
                int type = sqlite3_column_type(stmt, i);
                if (type == SQLITE_INTEGER) row_dict->elements[col_name] = Value((double)sqlite3_column_int64(stmt, i));
                else if (type == SQLITE_FLOAT) row_dict->elements[col_name] = Value(sqlite3_column_double(stmt, i));
                else if (type == SQLITE_TEXT) {
                    const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                    row_dict->elements[col_name] = txt ? Value(vm->arena().allocate_string(txt)) : Value();
                }
                else row_dict->elements[col_name] = Value();
            }
        }
        sqlite3_finalize(stmt);
        return Value(result_list);
    });

    NativeRegistry::register_builtin(vm, "std_db_insert", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 2) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_insert expects 2 arguments"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string model = vm->peek(1).to_string();
        if (!is_valid_sql_identifier(model)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Invalid table identifier: " + model));
            return Value();
        }

        Value data = vm->peek(0);
        if (!data.is_dict()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_insert expects data to be a dictionary"));
            return Value();
        }
        ObjDict* dict = static_cast<ObjDict*>(data.get_obj());

        std::string cols = "";
        std::string vals = "";
        for (auto& pair : dict->elements) {
            if (!is_valid_sql_identifier(pair.first)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string("Invalid column identifier: " + pair.first));
                return Value();
            }
            if (!cols.empty()) { cols += ", "; vals += ", "; }
            cols += quote_identifier(pair.first);
            vals += "?";
        }
        std::string q = "INSERT INTO " + quote_identifier(model) + " (" + cols + ") VALUES (" + vals + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(conn, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int i = 1;
            for (auto& pair : dict->elements) {
                if (pair.second.is_null()) sqlite3_bind_null(stmt, i++);
                else if (pair.second.is_number()) sqlite3_bind_double(stmt, i++, pair.second.as_number());
                else if (pair.second.is_bool()) sqlite3_bind_int(stmt, i++, pair.second.as_bool() ? 1 : 0);
                else sqlite3_bind_text(stmt, i++, pair.second.to_string().c_str(), -1, SQLITE_TRANSIENT);
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } else {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Failed to prepare INSERT query: " + std::string(sqlite3_errmsg(conn))));
        }
        return Value();
    });

    NativeRegistry::register_builtin(vm, "std_db_find", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 3) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_find expects 3 arguments"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string model = vm->peek(2).to_string();
        if (!is_valid_sql_identifier(model)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Invalid table identifier: " + model));
            return Value();
        }
        Value conds = vm->peek(1);
        bool find_all = vm->peek(0).as_bool();

        std::string err_out;
        auto where_info = build_where_clause(conds, err_out);
        if (!err_out.empty()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err_out));
            return Value();
        }

        std::string q = "SELECT * FROM " + quote_identifier(model) + where_info.first;
        if (!find_all) {
            q += " LIMIT 1";
        }
        sqlite3_stmt* stmt = nullptr;
        auto result_list = vm->arena().allocate<ObjList>();
        GCRootGuard res_guard(vm->arena(), result_list);
        if (sqlite3_prepare_v2(conn, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int idx = 1;
            for (auto& val : where_info.second) {
                if (val.is_null()) sqlite3_bind_null(stmt, idx++);
                else if (val.is_number()) sqlite3_bind_double(stmt, idx++, val.as_number());
                else if (val.is_bool()) sqlite3_bind_int(stmt, idx++, val.as_bool() ? 1 : 0);
                else sqlite3_bind_text(stmt, idx++, val.to_string().c_str(), -1, SQLITE_TRANSIENT);
            }

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto row_dict = vm->arena().allocate<ObjDict>();
                result_list->elements.push_back(Value(row_dict));
                int cols = sqlite3_column_count(stmt);
                for (int i = 0; i < cols; ++i) {
                    std::string col_name = sqlite3_column_name(stmt, i);
                    int type = sqlite3_column_type(stmt, i);
                    if (type == SQLITE_INTEGER) row_dict->elements[col_name] = Value((double)sqlite3_column_int64(stmt, i));
                    else if (type == SQLITE_FLOAT) row_dict->elements[col_name] = Value(sqlite3_column_double(stmt, i));
                    else if (type == SQLITE_TEXT) {
                        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                        row_dict->elements[col_name] = txt ? Value(vm->arena().allocate_string(txt)) : Value();
                    }
                    else row_dict->elements[col_name] = Value();
                }
            }
            sqlite3_finalize(stmt);
        } else {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Failed to prepare SELECT query: " + std::string(sqlite3_errmsg(conn))));
            return Value();
        }

        if (!find_all) {
            if (result_list->elements.empty()) return Value();
            return result_list->elements[0];
        }
        return Value(result_list);
    });

    NativeRegistry::register_builtin(vm, "std_db_update", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 3) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_update expects 3 arguments"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string model = vm->peek(2).to_string();
        if (!is_valid_sql_identifier(model)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Invalid table identifier: " + model));
            return Value();
        }
        Value conds = vm->peek(1);
        Value updates = vm->peek(0);
        if (!updates.is_dict()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_update expects third argument to be updates dictionary"));
            return Value();
        }
        auto* dict = static_cast<ObjDict*>(updates.get_obj());

        std::string set_clause = "";
        std::vector<Value> bind_vals;
        for (auto& pair : dict->elements) {
            if (!is_valid_sql_identifier(pair.first)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string("Invalid column identifier in updates: " + pair.first));
                return Value();
            }
            if (!set_clause.empty()) set_clause += ", ";
            set_clause += quote_identifier(pair.first) + " = ?";
            bind_vals.push_back(pair.second);
        }

        std::string err_out;
        auto where_info = build_where_clause(conds, err_out);
        if (!err_out.empty()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err_out));
            return Value();
        }

        std::string q = "UPDATE " + quote_identifier(model) + " SET " + set_clause + where_info.first;
        bind_vals.insert(bind_vals.end(), where_info.second.begin(), where_info.second.end());

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(conn, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int idx = 1;
            for (auto& val : bind_vals) {
                if (val.is_null()) sqlite3_bind_null(stmt, idx++);
                else if (val.is_number()) sqlite3_bind_double(stmt, idx++, val.as_number());
                else if (val.is_bool()) sqlite3_bind_int(stmt, idx++, val.as_bool() ? 1 : 0);
                else sqlite3_bind_text(stmt, idx++, val.to_string().c_str(), -1, SQLITE_TRANSIENT);
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } else {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Failed to prepare UPDATE query: " + std::string(sqlite3_errmsg(conn))));
        }
        return Value();
    });

    NativeRegistry::register_builtin(vm, "std_db_delete", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 2) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_delete expects 2 arguments"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string model = vm->peek(1).to_string();
        if (!is_valid_sql_identifier(model)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Invalid table identifier: " + model));
            return Value();
        }
        Value conds = vm->peek(0);

        std::string err_out;
        auto where_info = build_where_clause(conds, err_out);
        if (!err_out.empty()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err_out));
            return Value();
        }

        std::string q = "DELETE FROM " + quote_identifier(model) + where_info.first;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(conn, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int idx = 1;
            for (auto& val : where_info.second) {
                if (val.is_null()) sqlite3_bind_null(stmt, idx++);
                else if (val.is_number()) sqlite3_bind_double(stmt, idx++, val.as_number());
                else if (val.is_bool()) sqlite3_bind_int(stmt, idx++, val.as_bool() ? 1 : 0);
                else sqlite3_bind_text(stmt, idx++, val.to_string().c_str(), -1, SQLITE_TRANSIENT);
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } else {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Failed to prepare DELETE query: " + std::string(sqlite3_errmsg(conn))));
        }
        return Value();
    });

    NativeRegistry::register_builtin(vm, "std_db_model", -1, [](VM* vm, int arg_count) -> Value {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (arg_count != 2) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_model expects 2 arguments"));
            return Value();
        }
        sqlite3* conn = resolve_db_conn(vm, arg_count);
        if (!conn) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Database not open. Call db_open() or connect_database() first."));
            return Value();
        }
        std::string name = vm->peek(1).to_string();
        if (!is_valid_sql_identifier(name)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("Invalid table identifier: " + name));
            return Value();
        }

        Value fields_val = vm->peek(0);
        if (!fields_val.is_dict()) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("db_model expects fields to be a dictionary"));
            return Value();
        }
        ObjDict* fields = static_cast<ObjDict*>(fields_val.get_obj());

        std::string sql = "CREATE TABLE IF NOT EXISTS " + quote_identifier(name) + " (id INTEGER PRIMARY KEY AUTOINCREMENT";
        for (auto& pair : fields->elements) {
            if (!is_valid_sql_identifier(pair.first)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string("Invalid field identifier: " + pair.first));
                return Value();
            }
            sql += ", " + quote_identifier(pair.first) + " ";
            std::string type = pair.second.to_string();
            if (type == "int" || type == "integer") sql += "INTEGER";
            else if (type == "float" || type == "number" || type == "real") sql += "REAL";
            else if (type == "bool" || type == "boolean") sql += "INTEGER";
            else sql += "TEXT";
        }
        sql += ");";

        char* err_msg = nullptr;
        if (sqlite3_exec(conn, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "Unknown SQL error";
            sqlite3_free(err_msg);
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("DDL Error: " + err));
            return Value();
        }

        return Value(true);
    });

    vm->globals->values["db_open"] = vm->globals->values["std_db_open"];
    vm->globals->values["db_close"] = vm->globals->values["std_db_close"];
    vm->globals->values["db_query"] = vm->globals->values["std_db_query_rows"];
}

} // namespace shell_lite
