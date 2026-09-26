// [store] 极简内存仓库，模板只能放头文件。
//
// 注意：unordered_map 的节点地址是稳定的（rehash 只动桶），
// 所以外面拿着 T* 用是安全的 —— 只要别把那个元素删了。
// 这跟 vector 不同：vector 一扩容，之前拿的指针 / 引用全废。
#pragma once
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Types.h"

namespace eats {

    template <class T>
    class Repo {
    public:
        // 没填 id 的话这里帮忙分配一个
        T& add(T value) {
            if (value.id == kNoId) value.id = nextId();
            const Id id = value.id;
            auto [it, inserted] = items_.try_emplace(id, std::move(value));
            if (inserted) keys_.push_back(id);
            return it->second;
        }

        T* find(Id id) {
            auto it = items_.find(id);
            return it == items_.end() ? nullptr : &it->second;
        }

        const T* find(Id id) const {
            auto it = items_.find(id);
            return it == items_.end() ? nullptr : &it->second;
        }

        bool contains(Id id) const { return items_.contains(id); }

        bool erase(Id id) {
            if (items_.erase(id) == 0) return false;
            std::erase(keys_, id);
            return true;
        }

        std::vector<T*> all() {
            std::vector<T*> out;
            out.reserve(keys_.size());
            for (const Id id : keys_) out.push_back(&items_.at(id));
            return out;
        }

        std::vector<const T*> all() const {
            std::vector<const T*> out;
            out.reserve(keys_.size());
            for (const Id id : keys_) out.push_back(&items_.at(id));
            return out;
        }

        template <class Pred>
        std::vector<T*> where(Pred pred) {
            std::vector<T*> out;
            for (const Id id : keys_) {
                T& item = items_.at(id);
                if (pred(item)) out.push_back(&item);
            }
            return out;
        }

        std::size_t size()  const { return items_.size(); }
        bool        empty() const { return items_.empty(); }

    private:
        std::unordered_map<Id, T> items_;
        std::vector<Id> keys_;   // 记住插入顺序，列表每次刷新才不会跳来跳去
    };

}  // namespace eats