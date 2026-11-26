#include "skeet/network/active_sound_retention.hpp"

namespace skeet::network {
namespace {
using Tree = esp::ActiveSoundTree;
using Node = esp::ActiveSoundNode;
Node& node(Tree& tree, std::int32_t index) noexcept {
    return const_cast<Node&>(tree.nodes[index]);
}
std::int32_t left(const Tree& tree, std::int32_t index) noexcept {
    return index == -1 ? -1 : tree.nodes[index].left; //433B91A5, nil4347262C
}
std::int32_t right(const Tree& tree, std::int32_t index) noexcept {
    return index == -1 ? -1 : tree.nodes[index].right; //433C82B6
}
std::int32_t parent(const Tree& tree, std::int32_t index) noexcept {
    return index == -1 ? -1 : tree.nodes[index].parent; //4341A66B
}
std::uint32_t color(const Tree& tree, std::int32_t index) noexcept {
    return index == -1 ? 1u : tree.nodes[index].unused_0c; //43340117
}
bool black(const Tree& tree, std::int32_t index) noexcept {
    return color(tree, index) == 1u; //433F4A70 uses equality, not nonzero
}
void rotate_left(Tree& tree, std::int32_t index) noexcept { //433ED2B2
    const auto child = right(tree, index);
    node(tree, index).right = left(tree, child);
    if (left(tree, child) != -1)
        node(tree, left(tree, child)).parent = index;
    if (child != -1)
        node(tree, child).parent = parent(tree, index);
    if (index == tree.root)
        tree.root = child;
    else if (left(tree, parent(tree, index)) == index)
        node(tree, parent(tree, index)).left = child;
    else
        node(tree, parent(tree, index)).right = child;
    node(tree, child).left = index;
    if (index != -1)
        node(tree, index).parent = child;
}
void rotate_right(Tree& tree, std::int32_t index) noexcept { //433F1EEC
    const auto child = left(tree, index);
    node(tree, index).left = right(tree, child);
    if (right(tree, child) != -1)
        node(tree, right(tree, child)).parent = index;
    if (child != -1)
        node(tree, child).parent = parent(tree, index);
    if (index == tree.root)
        tree.root = child;
    else if (right(tree, parent(tree, index)) == index)
        node(tree, parent(tree, index)).right = child;
    else
        node(tree, parent(tree, index)).left = child;
    node(tree, child).right = index;
    if (index != -1)
        node(tree, index).parent = child;
}
void fix_removed_black(Tree& tree, std::int32_t index) noexcept { //4334025F
    while (index != tree.root && black(tree, index)) {
        auto p = parent(tree, index);
        if (index == left(tree, p)) {
            auto sibling = right(tree, p);
            if (color(tree, sibling) == 0) {
                node(tree, sibling).unused_0c = 1;
                node(tree, p).unused_0c = 0;
                rotate_left(tree, p);
                p = parent(tree, index);
                sibling = right(tree, p);
            }
            if (black(tree, left(tree, sibling)) && black(tree, right(tree, sibling))) {
                if (sibling != -1)
                    node(tree, sibling).unused_0c = 0;
                index = p;
            } else {
                if (black(tree, right(tree, sibling))) {
                    node(tree, left(tree, sibling)).unused_0c = 1;
                    node(tree, sibling).unused_0c = 0;
                    rotate_right(tree, sibling);
                    p = parent(tree, index);
                    sibling = right(tree, p);
                }
                node(tree, sibling).unused_0c = color(tree, p);
                node(tree, p).unused_0c = 1;
                node(tree, right(tree, sibling)).unused_0c = 1;
                rotate_left(tree, p);
                index = tree.root;
            }
        } else {
            auto sibling = left(tree, p);
            if (color(tree, sibling) == 0) {
                node(tree, sibling).unused_0c = 1;
                node(tree, p).unused_0c = 0;
                rotate_right(tree, p);
                p = parent(tree, index);
                sibling = left(tree, p);
            }
            if (black(tree, right(tree, sibling)) && black(tree, left(tree, sibling))) {
                if (sibling != -1)
                    node(tree, sibling).unused_0c = 0;
                index = p;
            } else {
                if (black(tree, left(tree, sibling))) {
                    node(tree, right(tree, sibling)).unused_0c = 1;
                    node(tree, sibling).unused_0c = 0;
                    rotate_left(tree, sibling);
                    p = parent(tree, index);
                    sibling = left(tree, p);
                }
                node(tree, sibling).unused_0c = color(tree, p);
                node(tree, p).unused_0c = 1;
                node(tree, left(tree, sibling)).unused_0c = 1;
                rotate_right(tree, p);
                index = tree.root;
            }
        }
    }
    node(tree, index).unused_0c = 1;
}
void unlink(Tree& tree, std::int32_t index) noexcept { //complete43359947
    if (index == -1)
        return;
    auto removed = index;
    if (left(tree, index) != -1 && right(tree, index) != -1) {
        removed = right(tree, index);
        while (left(tree, removed) != -1)
            removed = left(tree, removed);
    }
    const auto child = left(tree, removed) != -1 ? left(tree, removed) : right(tree, removed);
    if (child != -1)
        node(tree, child).parent = parent(tree, removed);
    if (removed == tree.root)
        tree.root = child;
    else if (left(tree, parent(tree, removed)) == removed)
        node(tree, parent(tree, removed)).left = child;
    else
        node(tree, parent(tree, removed)).right = child;
    const auto removed_color = color(tree, removed);
    if (removed != index) {
        node(tree, removed).parent = parent(tree, index);
        node(tree, removed).right = right(tree, index);
        node(tree, removed).left = left(tree, index);
        if (index == tree.root)
            tree.root = removed;
        else if (left(tree, parent(tree, index)) == index)
            node(tree, parent(tree, index)).left = removed;
        else
            node(tree, parent(tree, index)).right = removed;
        if (left(tree, removed) != -1)
            node(tree, left(tree, removed)).parent = removed;
        if (right(tree, removed) != -1)
            node(tree, right(tree, removed)).parent = removed;
        node(tree, removed).unused_0c = color(tree, index);
    }
    // Native does not run the fixup for a nil child. Preserve that branch.
    if (child != -1 && removed_color == 1)
        fix_removed_black(tree, child);
}
} //namespace
std::int32_t first_active_sound(const esp::ActiveSoundTree& tree) noexcept {
    auto index = tree.root;
    while (left(tree, index) != -1)
        index = left(tree, index);
    return index;
}
std::int32_t next_active_sound(const esp::ActiveSoundTree& tree, std::int32_t index) noexcept {
    if (std::uint32_t(index) >= tree.allocation_count || index > tree.last_allocated ||
        left(tree, index) == index)
        return -1;
    if (right(tree, index) != -1) {
        index = right(tree, index);
        while (left(tree, index) != -1)
            index = left(tree, index);
        return index;
    }
    auto p = parent(tree, index);
    while (p != -1 && right(tree, p) == index) {
        index = p;
        p = parent(tree, p);
    }
    return p;
}
void release_active_sound(esp::ActiveSoundTree& tree, std::int32_t index) noexcept {
    unlink(tree, index);
    node(tree, index).left = index;
    node(tree, index).right = static_cast<std::int32_t>(tree.unused_18);
    --tree.count;
    tree.unused_18 = static_cast<std::uint32_t>(index);
}
} //namespace skeet::network
