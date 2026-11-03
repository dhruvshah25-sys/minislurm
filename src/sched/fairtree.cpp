#include "sched/fairtree.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace msl {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();

std::uint32_t count_users(const Assoc& a) {
    std::uint32_t n = a.is_user ? 1u : 0u;
    for (const auto& c : a.children) n += count_users(*c);
    return n;
}

void visit_levels(Assoc& a, double total_usage, const auto& compute_level) {
    if (a.children.empty()) return;
    std::vector<Assoc*> sib;
    sib.reserve(a.children.size());
    for (auto& c : a.children) sib.push_back(c.get());
    compute_level(sib);
    for (Assoc* c : sib) {
        c->effective_usage = total_usage > 0.0 ? c->raw_usage / total_usage : 0.0;
        visit_levels(*c, total_usage, compute_level);
    }
}
}  // namespace

AssocTree::AssocTree() : root_(std::make_unique<Assoc>()) {
    root_->name = "root";
}

Assoc& AssocTree::add_account(Assoc& parent, std::string name, std::uint32_t shares) {
    if (parent.is_user) throw std::invalid_argument("users cannot have children");
    auto a = std::make_unique<Assoc>();
    a->name = std::move(name);
    a->raw_shares = shares;
    a->parent = &parent;
    parent.children.push_back(std::move(a));
    return *parent.children.back();
}

Assoc& AssocTree::add_user(Assoc& account, std::string name, std::uint32_t shares, double usage) {
    Assoc& u = add_account(account, std::move(name), shares);
    u.is_user = true;
    u.raw_usage = usage;
    return u;
}

double AssocTree::sum_usage(Assoc& a) {
    if (a.is_user) return a.raw_usage;
    double s = 0.0;
    for (auto& c : a.children) s += sum_usage(*c);
    a.raw_usage = s;
    return s;
}

void AssocTree::compute_level(std::vector<Assoc*>& siblings) {
    double shares = 0.0, usage = 0.0;
    for (const Assoc* a : siblings) {
        shares += a->raw_shares;
        usage += a->raw_usage;
    }
    for (Assoc* a : siblings) {
        const double S = shares > 0.0 ? a->raw_shares / shares : 0.0;
        const double U = usage > 0.0 ? a->raw_usage / usage : 0.0;
        a->norm_shares = S;
        if (S == 0.0)      a->level_fs = 0.0;   // no shares: always last
        else if (U == 0.0) a->level_fs = kInf;  // unused: always first
        else               a->level_fs = S / U;
    }
}

void AssocTree::rank_level(std::vector<Assoc*> siblings) {
    // Descending LevelFS; name as a deterministic tie-break for display order.
    std::sort(siblings.begin(), siblings.end(), [](const Assoc* a, const Assoc* b) {
        if (a->level_fs != b->level_fs) return a->level_fs > b->level_fs;
        return a->name < b->name;
    });

    const Assoc* prev_user = nullptr;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        Assoc* a = siblings[i];

        if (a->is_user) {
            const bool tied = prev_user && prev_user->level_fs == a->level_fs;
            if (!tied) rank_ = rnt_;
            a->rank = (a->raw_shares == 0) ? 0 : rank_;
            a->fairshare = user_count_ ? static_cast<double>(a->rank) / user_count_ : 0.0;
            if (rnt_ > 0) --rnt_;
            prev_user = a;
            rank_order_.push_back(a);
            continue;
        }

        // Account. If several sibling accounts have the same LevelFS, Fair
        // Tree can't say which is "more deserving", so it merges their
        // children into one list and ranks them together.
        std::vector<Assoc*> merged(a->children.size());
        std::transform(a->children.begin(), a->children.end(), merged.begin(),
                       [](auto& c) { return c.get(); });
        while (i + 1 < siblings.size() && !siblings[i + 1]->is_user &&
               siblings[i + 1]->level_fs == a->level_fs) {
            ++i;
            for (auto& c : siblings[i]->children) merged.push_back(c.get());
        }
        rank_level(std::move(merged));
        prev_user = nullptr;
    }
}

void AssocTree::compute() {
    user_count_ = count_users(*root_);
    const double total = sum_usage(*root_);

    root_->norm_shares = 1.0;
    root_->effective_usage = total > 0.0 ? 1.0 : 0.0;
    visit_levels(*root_, total, [this](std::vector<Assoc*>& s) { compute_level(s); });

    rank_order_.clear();
    rank_ = rnt_ = user_count_;
    std::vector<Assoc*> top;
    for (auto& c : root_->children) top.push_back(c.get());
    rank_level(std::move(top));
}

Assoc* AssocTree::find_user(const std::string& account, const std::string& user) {
    // Small trees: a linear walk is fine. A real controller would hash.
    std::vector<Assoc*> stack{root_.get()};
    while (!stack.empty()) {
        Assoc* a = stack.back();
        stack.pop_back();
        if (a->is_user && a->name == user && a->account() == account) return a;
        for (auto& c : a->children) stack.push_back(c.get());
    }
    return nullptr;
}

const Assoc* AssocTree::find_user(const std::string& account, const std::string& user) const {
    return const_cast<AssocTree*>(this)->find_user(account, user);
}

}  // namespace msl
