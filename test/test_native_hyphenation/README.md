# Hyphenator Tests

This directory contains comprehensive unit tests for the `Hyphenator` class, which implements word-splitting and hyphenation logic for both Latin and Cyrillic scripts.

## Test Coverage

The test suite includes **44 test cases** covering:

### Latin Text Hyphenation
- Basic word splitting with syllable boundaries
- Consonant cluster handling
- Diphthong detection (ai, ea, oi, etc.)
- English onset patterns (ch, sh, th, pr, tr, etc.)
- Multiple vowel words

### Cyrillic Text Hyphenation
- Russian syllable rules
- Soft sign (ь) and hard sign (ъ) handling
- Cyrillic vowel detection (including ё)
- Cyrillic onset patterns
- Consonant cluster handling

### Edge Cases
- Empty strings and single characters
- Mixed script text (Latin + Cyrillic)
- Words below minimum length (< 5 codepoints)
- Words with apostrophes
- Non-alphabetic characters (numbers)
- Mixed alphanumeric content
- All-consonant words
- Single-vowel words
- Case sensitivity (uppercase, lowercase, mixed)

### Force Split & Fallback
- Width-based splitting when linguistic rules don't apply
- Force split behavior for non-alphabetic content
- Fallback mechanisms when no natural break points exist
- Width constraints and boundary conditions

### Result Validation
- Hyphen presence in head segment
- Non-empty head and tail segments
- Recombination to original word
- Null pointer handling
- Invalid width handling (negative, zero)

## Running the Tests

### Option 1: Using the Shell Script (Recommended for quick testing)

```bash
cd test/test_native_hyphenation
./run_tests.sh
```

This script will:
1. Download the Unity test framework if needed
2. Compile the tests with g++
3. Run all 44 test cases
4. Report pass/fail status

### Option 2: Using PlatformIO (Integrated with build system)

```bash
# Run only the hyphenation tests
pio test -f test_native_hyphenation -e native

# Run all native tests
pio test -e native
```

### Option 3: Manual Compilation

```bash
# Compile
g++ -std=c++2a -DUNIT_TEST \
  -Itest/test_native_hyphenation \
  -Ilib -Ilib/EpdFont -Ilib/Utf8 \
  -Isrc -I. -I/path/to/Unity/src \
  test/test_native_hyphenation/test_main.cpp \
  /path/to/Unity/src/unity.c \
  -o test_hyphenation

# Run
./test_hyphenation
```

## Test Implementation Details

The tests use a mock `GfxRenderer` class (`GfxRenderer.h` stub) to avoid hardware dependencies. The mock renderer simulates character width calculations needed for width-based splitting decisions:
- Regular characters: 10 pixels
- Hyphen: 5 pixels

This allows testing the hyphenation logic without requiring the actual e-ink display hardware or SDL dependencies.

## Files

- `test_main.cpp` - Main test file with all 44 test cases
- `GfxRenderer.h` - Mock GfxRenderer class to avoid hardware dependencies
- `run_tests.sh` - Convenience script for quick test execution
- `README.md` - This file

## Adding New Tests

To add new tests:

1. Add a new test function following the naming pattern `test_*`:
   ```cpp
   void test_my_new_case() {
     std::string head, tail;
     bool result = canSplitWord("myword", 100, &head, &tail);
     TEST_ASSERT_TRUE(result);
     // Additional assertions...
   }
   ```

2. Register the test in `main()`:
   ```cpp
   RUN_TEST(test_my_new_case);
   ```

3. Recompile and run to verify the new test.

## Expected Results

All 44 tests should pass:
```
44 Tests 0 Failures 0 Ignored 
OK
```

If any tests fail, review the assertion messages to understand which behavior isn't working as expected.
