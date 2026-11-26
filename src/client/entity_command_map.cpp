#include "entity_command_map.hpp"
#include "entity_command_memory.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/hash_growth.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <new>

namespace skeet::client::entity_command_detail {
namespace {
// Exact256-word read-only growth table43473248 in the pinned original.
constexpr std::array<std::uint32_t, 256> native_model_category_primes{
    2u,          3u,          5u,          7u,          11u,         13u,         17u,
    19u,         23u,         29u,         31u,         37u,         41u,         43u,
    47u,         53u,         59u,         61u,         67u,         71u,         73u,
    79u,         83u,         89u,         97u,         103u,        109u,        113u,
    127u,        137u,        139u,        149u,        157u,        167u,        179u,
    193u,        199u,        211u,        227u,        241u,        257u,        277u,
    293u,        313u,        337u,        359u,        383u,        409u,        439u,
    467u,        503u,        541u,        577u,        619u,        661u,        709u,
    761u,        823u,        887u,        953u,        1031u,       1109u,       1193u,
    1289u,       1381u,       1493u,       1613u,       1741u,       1879u,       2029u,
    2179u,       2357u,       2549u,       2753u,       2971u,       3209u,       3469u,
    3739u,       4027u,       4349u,       4703u,       5087u,       5503u,       5953u,
    6427u,       6949u,       7517u,       8123u,       8783u,       9497u,       10273u,
    11113u,      12011u,      12983u,      14033u,      15173u,      16411u,      17749u,
    19183u,      20753u,      22447u,      24281u,      26267u,      28411u,      30727u,
    33223u,      35933u,      38873u,      42043u,      45481u,      49201u,      53201u,
    57557u,      62233u,      67307u,      72817u,      78779u,      85229u,      92203u,
    99733u,      107897u,     116731u,     126271u,     136607u,     147793u,     159871u,
    172933u,     187091u,     202409u,     218971u,     236897u,     256279u,     277261u,
    299951u,     324503u,     351061u,     379787u,     410857u,     444487u,     480881u,
    520241u,     562841u,     608903u,     658753u,     712697u,     771049u,     834181u,
    902483u,     976369u,     1056323u,    1142821u,    1236397u,    1337629u,    1447153u,
    1565659u,    1693859u,    1832561u,    1982627u,    2144977u,    2320627u,    2510653u,
    2716249u,    2938679u,    3179303u,    3439651u,    3721303u,    4026031u,    4355707u,
    4712381u,    5098259u,    5515729u,    5967347u,    6456007u,    6984629u,    7556579u,
    8175383u,    8844859u,    9569143u,    10352717u,   11200489u,   12117689u,   13109983u,
    14183539u,   15345007u,   16601593u,   17961079u,   19431899u,   21023161u,   22744717u,
    24607243u,   26622317u,   28802401u,   31160981u,   33712729u,   36473443u,   39460231u,
    42691603u,   46187573u,   49969847u,   54061849u,   58488943u,   63278561u,   68460391u,
    74066549u,   80131819u,   86693767u,   93793069u,   101473717u,  109783337u,  118773397u,
    128499677u,  139022417u,  150406843u,  162723577u,  176048909u,  190465427u,  206062531u,
    222936881u,  241193053u,  260944219u,  282312799u,  305431229u,  330442829u,  357502601u,
    386778277u,  418451333u,  452718089u,  489790921u,  529899637u,  573292817u,  620239453u,
    671030513u,  725980837u,  785430967u,  849749479u,  919334987u,  994618837u,  1076067617u,
    1164186217u, 1259520799u, 1362662261u, 1474249943u, 1594975441u, 1725587117u, 1866894511u,
    2019773507u, 2185171673u, 2364114217u, 2557710269u, 2767159799u, 2993761039u, 3238918481u,
    3504151727u, 3791104843u, 4101556399u, 4294967291u};
struct TokenInput {
    std::uint8_t& initialize;
    std::array<std::uint32_t, 8>& delimiters;

    //433EA7BC. Map entity text uses the native ASCII token contract. Quoted
    //and ordinary tokens retain the original 0x800-byte limit behavior.
    const char* read(const char* cursor, char* output) const {
        std::uint32_t length = 0;
        output[0] = 0;
        if (!cursor)
            return nullptr;
        if (initialize) {
            initialize = 0;
            delimiters.fill(0);
            constexpr char symbols[]{'{', '}', '(', ')', '\'', '"'};
            for (const unsigned char symbol : symbols)
                delimiters[symbol >> 5] |= 1u << (symbol & 31u);
        }
        std::int32_t value;
        for (;;) {
            value = static_cast<signed char>(*cursor);
            while (value <= 0x20) {
                if (!value)
                    return nullptr;
                value = static_cast<signed char>(*++cursor);
            }
            if (value != '/' || cursor[1] != '/')
                break;
            while (*cursor && *cursor != '\n')
                ++cursor;
        }
        if (value == '"') {
            ++cursor;
            for (;;) {
                const char byte = *cursor++;
                value = static_cast<signed char>(byte);
                if (value == '"' || !byte) {
                    output[length] = 0;
                    return cursor;
                }
                const auto previous = length;
                output[length++] = byte;
                if (length < 0x800u)
                    continue;
                length = previous;
                output[length] = 0;
                break;
            }
        }
        const auto delimiter = [&](std::int32_t byte) {
            const auto bits = std::uint32_t(byte);
            return (delimiters[bits >> 5] & (1u << (bits & 31u))) != 0;
        };
        if (delimiter(value)) {
            output[length] = char(value);
            output[length + 1] = 0;
            return cursor + 1;
        }
        do {
            ++cursor;
            output[length] = char(value);
            const auto previous = length++;
            value = static_cast<signed char>(*cursor);
            if (delimiter(value))
                break;
            if (length >= 0x800u) {
                length = previous;
                output[length] = 0;
            }
        } while (value > 0x20);
        output[length] = 0;
        return cursor;
    }
};
struct EntityText {
    const char* begin;
    std::int32_t field_04 = -1;
    const char* cursor;
};
using Token = std::array<char, 0x801>;

bool find_property(const TokenInput& input, const EntityText& entity, std::uint32_t hash,
                   char* output) { //433FBD9F
    Token token;
    auto* cursor = entity.begin;
    while (cursor) {
        cursor = input.read(cursor, token.data());
        if (token[0] == '}')
            return false;
        if (engine::netvar_name_hash(token.data()) == hash) {
            input.read(cursor, token.data());
            std::strncpy(output, token.data(), 0x800);
            return true;
        }
        cursor = input.read(cursor, token.data());
    }
    return false;
}
bool next_property(const TokenInput& input, EntityText& entity, char* key, char* value) { //4340C6B1
    Token token;
    const auto* previous = entity.cursor;
    entity.cursor = input.read(previous, token.data());
    if (token[0] == '}') {
        entity.cursor = previous;
        return false;
    }
    if (!entity.cursor)
        return false;
    std::strncpy(key, token.data(), 0x800);
    auto length = std::strlen(key);
    while (length && key[length - 1] == ' ')
        key[--length] = 0;
    entity.cursor = input.read(entity.cursor, token.data());
    if (!entity.cursor || token[0] == '}')
        return false;
    std::strncpy(value, token.data(), 0x800);
    return true;
}
}

void ModelCategories::initialize() noexcept {
    auto& initial = image_.native_cell<std::array<EntityModelCategoryNode*, 2>>(0x4346935Cu);
    view_.buckets = initial;
    count_ = 0;
    next_resize_ = 0;
    maximum_load_ = 1.0f;
    multiplier_ = 2.0f;
}
void ModelCategories::clear() {
    const auto bucket_count = view_.buckets.size() - 1;
    auto** buckets = const_cast<EntityModelCategoryNode**>(view_.buckets.data());
    for (std::size_t index = 0; index < bucket_count; ++index) {
        auto* node = buckets[index];
        while (node) {
            auto* next = node->next;
            free(memory_, node);
            node = next;
        }
        buckets[index] = nullptr;
    }
    count_ = 0;
}
void ModelCategories::rehash(std::uint32_t count) {
    auto** buckets = static_cast<EntityModelCategoryNode**>(
        allocate(memory_, (std::size_t(count) + 1) * sizeof(EntityModelCategoryNode*)));
    std::memset(buckets, 0, std::size_t(count) * sizeof(EntityModelCategoryNode*));
    buckets[count] = reinterpret_cast<EntityModelCategoryNode*>(~std::uintptr_t{});
    const auto old_count = view_.buckets.size() - 1;
    auto** old = const_cast<EntityModelCategoryNode**>(view_.buckets.data());
    for (std::size_t index = 0; index < old_count; ++index) {
        while (auto* node = old[index]) {
            const auto bucket = node->model_hash % count;
            old[index] = node->next;
            node->next = buckets[bucket];
            buckets[bucket] = node;
        }
    }
    if (old_count > 1)
        free(memory_, old);
    view_.buckets = {buckets, std::size_t(count) + 1};
}
EntityModelCategoryNode& ModelCategories::find_or_insert(const std::uint32_t* key) {
    const auto hash = *key;
    const auto bucket_count = std::uint32_t(view_.buckets.size() - 1);
    auto bucket = hash % bucket_count;
    for (auto* node = view_.buckets[bucket]; node; node = node->next)
        if (node->model_hash == hash)
            return *node;
    const auto growth =
        engine::plan_native_hash_growth(bucket_count, count_, maximum_load_, multiplier_,
                                        next_resize_, native_model_category_primes, ceiling_);
    auto* node = ::new (allocate(memory_, sizeof(EntityModelCategoryNode)))
        EntityModelCategoryNode{*key, 0, nullptr};
    if (growth.rehash) {
        bucket = hash % growth.bucket_count;
        rehash(growth.bucket_count);
    }
    auto** buckets = const_cast<EntityModelCategoryNode**>(view_.buckets.data());
    node->next = buckets[bucket];
    buckets[bucket] = node;
    ++count_;
    return *node;
}
void ModelCategories::parse(const char* text) {
    clear();
    const TokenInput input{image_.native_cell<std::uint8_t>(0x4346D678u),
                           image_.native_cell<std::array<std::uint32_t, 8>>(0x4347C620u)};
    Token token, key, value, temporary;
    auto* cursor = text;
    while ((cursor = input.read(cursor, token.data()))) {
        if (token[0] == '{') {
            EntityText entity{cursor, -1, cursor};
            if (find_property(input, entity, 0xE5EA520Du, temporary.data())) {
                const auto category = engine::netvar_name_hash(temporary.data());
                if (category == 0xA717EF32u || category == 0x96FE2D31u || category == 0x54770CA4u ||
                    category == 0x0A6154BFu) {
                    if (find_property(input, entity, 0xF952B07Eu, temporary.data()) &&
                        std::atoi(temporary.data()) != 0 &&
                        find_property(input, entity, 0x6EA9233Au, temporary.data()) &&
                        !(std::atoi(temporary.data()) & 1) &&
                        find_property(input, entity, 0x22413BABu, temporary.data())) {
                        auto model = engine::netvar_name_hash(temporary.data());
                        find_or_insert(&model).category = category;
                    }
                }
            }
            while (next_property(input, entity, key.data(), value.data())) {
            }
            cursor = entity.cursor;
        }
        if (!cursor)
            break;
        auto depth = 1;
        do {
            cursor = input.read(cursor, token.data());
            if (token[0] == '{' && !token[1])
                ++depth;
            else if (token[0] == '}' && !token[1] && !--depth)
                break;
        } while (cursor);
    }
}
void ModelCategories::release() {
    clear();
    if (view_.buckets.size() > 2)
        free(memory_, const_cast<EntityModelCategoryNode**>(view_.buckets.data()));
}
} // namespace skeet::client::entity_command_detail
