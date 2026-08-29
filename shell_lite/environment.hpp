#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string_view>
#include <mutex>
#include "value.hpp"

namespace shell_lite {

class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr) : parent_(parent) {}

    void set(std::string_view name, Value value) {
        if (is_constant(name)) throw std::runtime_error("Cannot reassign constant '" + std::string(name) + "'");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (variables_.count(name)) {
                variables_[name] = std::move(value);
                return;
            }
        }

        if (parent_ && parent_->update_if_exists(name, value)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        variables_[name] = std::move(value);
    }

    void set_const(std::string_view name, Value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (variables_.count(name)) throw std::runtime_error("Variable '" + std::string(name) + "' already declared");
        variables_[name] = std::move(value);
        constants_.insert(std::string(name));
    }

    Value get(std::string_view name) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = variables_.find(name);
            if (it != variables_.end()) return it->second;
        }
        if (parent_) return parent_->get(name);
        throw std::runtime_error("Variable '" + std::string(name) + "' is not defined.");
    }

    bool is_constant(std::string_view name) const {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (constants_.count(std::string(name))) return true;
        }
        if (parent_) return parent_->is_constant(name);
        return false;
    }

    bool update_if_exists(std::string_view name, const Value& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = variables_.find(name);
            if (it != variables_.end()) {
                if (constants_.count(std::string(name))) throw std::runtime_error("Cannot reassign constant '" + std::string(name) + "'");
                it->second = value;
                return true;
            }
        }
        if (parent_) {
            return parent_->update_if_exists(name, value);
        }
        return false;
    }

    std::shared_ptr<Environment> parent() const { return parent_; }

    void mark() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : variables_) {
            pair.second.mark();
        }
    }

private:
    std::shared_ptr<Environment> parent_;
    std::unordered_map<std::string_view, Value> variables_;
    std::unordered_set<std::string> constants_;
    mutable std::mutex mutex_;
};

}
