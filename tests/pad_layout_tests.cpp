// SPDX-License-Identifier: AGPL-3.0-only
// The pad layouts (src/app/pad_layout.*): Move rows and two MPC banks, the
// mapping both ways, as docs/MOVE_CONTROLS.md ("Pads") draws them.
#include "app/pad_layout.h"
#include <iostream>
#include <string>

using namespace app;

namespace {

int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; } } while (0)

// The pads as the Move lays them out, top row first: each pad's number
// (logical + 1) in the layout.
constexpr int rows_numbers[4][8] {
    {25, 26, 27, 28, 29, 30, 31, 32},
    {17, 18, 19, 20, 21, 22, 23, 24},
    { 9, 10, 11, 12, 13, 14, 15, 16},
    { 1,  2,  3,  4,  5,  6,  7,  8},
};
constexpr int banks_numbers[4][8] {
    {13, 14, 15, 16, 29, 30, 31, 32},
    { 9, 10, 11, 12, 25, 26, 27, 28},
    { 5,  6,  7,  8, 21, 22, 23, 24},
    { 1,  2,  3,  4, 17, 18, 19, 20},
};

void check_drawing(PadLayout layout, const int (&numbers)[4][8]) {
    for (int top_row = 0; top_row < 4; ++top_row)
        for (int column = 0; column < 8; ++column) {
            auto physical = uint8_t((3 - top_row) * 8 + column); // move::Input: bottom left first, eight a row
            auto logical = uint8_t(numbers[top_row][column] - 1);
            CHECK(logical_pad(physical, layout) == logical);
            CHECK(physical_pad(logical, layout) == physical);
        }
}

void check_both_ways(PadLayout layout) {
    bool seen[32] {};
    for (uint8_t physical = 0; physical < 32; ++physical) {
        uint8_t logical = logical_pad(physical, layout);
        CHECK(logical < 32);
        if (logical < 32) {
            CHECK(!seen[logical]); // each number once
            seen[logical] = true;
        }
        CHECK(physical_pad(logical, layout) == physical);
        CHECK(logical_pad(physical_pad(physical, layout), layout) == physical);
    }
}

} // namespace

int main() {
    check_drawing(PadLayout::Rows, rows_numbers);
    check_drawing(PadLayout::Banks, banks_numbers);
    check_both_ways(PadLayout::Rows);
    check_both_ways(PadLayout::Banks);

    // Rows: logical is physical.
    for (uint8_t pad = 0; pad < 32; ++pad) CHECK(logical_pad(pad, PadLayout::Rows) == pad);
    // Banks: the brief's formula, column by column.
    for (uint8_t physical = 0; physical < 32; ++physical) {
        int row = physical / 8, column = physical % 8;
        int expected = column < 4 ? row * 4 + column : 16 + row * 4 + (column - 4);
        CHECK(logical_pad(physical, PadLayout::Banks) == expected);
    }
    // Each bank is a 4x4: the left bank the left four columns, the right bank the right four.
    for (uint8_t logical = 0; logical < 32; ++logical) {
        uint8_t physical = physical_pad(logical, PadLayout::Banks);
        CHECK((physical % 8 < 4) == (logical < 16));
    }
    // Not a pad: handed back.
    CHECK(logical_pad(40, PadLayout::Banks) == 40);
    CHECK(physical_pad(40, PadLayout::Banks) == 40);
    // Usable where a constant is wanted (a table, a switch).
    static_assert(logical_pad(4, PadLayout::Banks) == 16, "the bottom row's fifth pad is the right bank's first");
    static_assert(physical_pad(4, PadLayout::Banks) == 8, "pad 5 is the left bank's second row");

    // Notes (Kerem's reading (a)). Rows: right a step, up a row `a_row`
    // steps, as today (chromatic fourths: C2 C#2 ... from the bottom left,
    // F2 above C2).
    CHECK(pad_steps(0, PadLayout::Rows, 5) == 0);
    CHECK(pad_steps(7, PadLayout::Rows, 5) == 7);
    CHECK(pad_steps(8, PadLayout::Rows, 5) == 5);
    CHECK(pad_steps(31, PadLayout::Rows, 5) == 7 + 15);
    CHECK(pad_steps(9, PadLayout::Rows, 3) == 4); // In-Key in 4ths, C major: three notes a row
    // Banks: the pad's number, 1-32 up the scale, the right bank going on
    // from the left one's top right.
    for (uint8_t physical = 0; physical < 32; ++physical)
        CHECK(pad_steps(physical, PadLayout::Banks, 5) == logical_pad(physical, PadLayout::Banks));
    CHECK(pad_steps(27, PadLayout::Banks, 5) == 15); // the left bank's top right: pad 16
    CHECK(pad_steps(4, PadLayout::Banks, 5) == 16);  // the right bank's bottom left: pad 17, the next note
    {   // Chromatic from C2 (48): every note from C2 to G4 once, none skipped.
        bool seen[32] {};
        for (uint8_t physical = 0; physical < 32; ++physical) seen[pad_steps(physical, PadLayout::Banks, 5)] = true;
        for (bool each : seen) CHECK(each);
    }
    {   // In-Key, C major from C2: the roots (C2 C3 C4 C5 C6) on pads 1, 8, 15, 22, 29.
        constexpr int major[7] {0, 2, 4, 5, 7, 9, 11};
        auto note = [&](uint8_t physical, PadLayout layout, int a_row) {
            int step = pad_steps(physical, layout, a_row);
            return 48 + 12 * (step / 7) + major[step % 7];
        };
        int roots = 0;
        for (uint8_t number = 0; number < 32; ++number)
            if (note(physical_pad(number, PadLayout::Banks), PadLayout::Banks, 3) % 12 == 0) {
                CHECK(number % 7 == 0);
                ++roots;
            }
        CHECK(roots == 5);
        CHECK(note(physical_pad(31, PadLayout::Banks), PadLayout::Banks, 3) == 101); // pad 32: F6
        CHECK(note(8, PadLayout::Rows, 3) == 53);                                     // Rows, In-Key 4ths: F2 above C2
    }

    // Typing: reading order from the top left; Delete bottom right in both.
    for (PadLayout layout : {PadLayout::Rows, PadLayout::Banks}) {
        bool seen[256] {};
        for (uint8_t physical = 0; physical < 32; ++physical) {
            auto key = uint8_t(typing_key(physical, layout));
            CHECK(key != 0 && !seen[key]);
            seen[key] = true;
        }
        CHECK(typing_key(24, layout) == 'a'); // the top left
        CHECK(typing_key(7, layout) == '\b'); // the bottom right
        CHECK(typing_key(32, layout) == 0);
    }
    CHECK(typing_key(31, PadLayout::Rows) == 'h');   // Rows: a-h across the top
    CHECK(typing_key(16, PadLayout::Rows) == 'i');
    CHECK(typing_key(27, PadLayout::Banks) == 'd');  // Banks: a-d the left bank's top row
    CHECK(typing_key(16, PadLayout::Banks) == 'e');  // then e-h under it
    CHECK(typing_key(3, PadLayout::Banks) == 'p');   // p the left bank's bottom right
    CHECK(typing_key(28, PadLayout::Banks) == 'q');  // q the right bank's top left
    CHECK(typing_key(4, PadLayout::Banks) == '.');   // the right bank's bottom row: . ' & Delete

    // The choice: Rows until set, then what was set.
    CHECK(pad_layout() == PadLayout::Rows);
    set_pad_layout(PadLayout::Banks);
    CHECK(pad_layout() == PadLayout::Banks);
    set_pad_layout(PadLayout::Rows);
    CHECK(pad_layout() == PadLayout::Rows);
    CHECK(std::string(pad_layout_name(PadLayout::Rows)) == "rows");
    CHECK(std::string(pad_layout_name(PadLayout::Banks)) == "banks");

    std::cout << "pad-layout-tests: " << checks - failures << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
