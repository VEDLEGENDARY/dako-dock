#include "../src/Shape.h"
#include <cstdio>
#include <cstdlib>

int main() {
    static_assert(shape::Surface > shape::Card && shape::Card > shape::Control && shape::Control > shape::Chip);
    static_assert(shape::Chip > 0 && shape::Fillet == shape::Surface);
    static_assert(shape::Pill(56) == shape::Surface && shape::Inner(shape::Surface, 12) == shape::Card);
    if (shape::Inner(shape::Surface, 200) != shape::Chip || shape::Pill(32) != shape::Card)
        std::abort();
    std::puts("PASS: positive ordered shape tokens, matching fillets, concentric insets and true pill radius.");
}
