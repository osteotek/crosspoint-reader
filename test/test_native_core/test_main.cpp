#include <Serialization/Serialization.h>
#include <Utf8.h>
#include <unity.h>

#include <sstream>
#include <string>

typedef struct {
  int32_t a;
  uint16_t b;
} PodSample;

void test_write_pod_roundtrip() {
  PodSample in{42, 0xBEEF};
  PodSample out{};
  std::stringstream buffer;

  serialization::writePod(buffer, in);
  serialization::readPod(buffer, out);

  TEST_ASSERT_EQUAL_INT32(in.a, out.a);
  TEST_ASSERT_EQUAL_UINT16(in.b, out.b);
}

void test_write_string_roundtrip() {
  const std::string original = "Привет EPUB";
  std::stringstream buffer;
  std::string restored;

  serialization::writeString(buffer, original);
  serialization::readString(buffer, restored);

  TEST_ASSERT_EQUAL_UINT32(original.size(), restored.size());
  TEST_ASSERT_EQUAL_STRING(original.c_str(), restored.c_str());
}

void test_write_string_embedded_null() {
  const std::string original{"abc\0def", 7};
  std::stringstream buffer;
  std::string restored;

  serialization::writeString(buffer, original);
  serialization::readString(buffer, restored);

  TEST_ASSERT_EQUAL_MEMORY(original.data(), restored.data(), original.size());
}

void test_utf8_ascii_progression() {
  const unsigned char text[] = "A";
  const unsigned char* ptr = text;

  TEST_ASSERT_EQUAL_UINT32('A', utf8NextCodepoint(&ptr));
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&ptr));
}

void test_utf8_multibyte_codepoint() {
  const unsigned char text[] = u8"Ж";  // U+0416
  const unsigned char* ptr = text;

  TEST_ASSERT_EQUAL_HEX32(0x0416, utf8NextCodepoint(&ptr));
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&ptr));
}

void test_utf8_four_byte_codepoint() {
  const unsigned char text[] = u8"😀";  // U+1F600
  const unsigned char* ptr = text;

  TEST_ASSERT_EQUAL_HEX32(0x1F600, utf8NextCodepoint(&ptr));
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&ptr));
}

void test_utf8_invalid_falls_back_to_single_byte() {
  const unsigned char text[] = {0xFF, 0x00};
  const unsigned char* ptr = text;

  TEST_ASSERT_EQUAL_HEX32(0xFF, utf8NextCodepoint(&ptr));
  TEST_ASSERT_EQUAL_UINT32(0, utf8NextCodepoint(&ptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_write_pod_roundtrip);
  RUN_TEST(test_write_string_roundtrip);
  RUN_TEST(test_write_string_embedded_null);
  RUN_TEST(test_utf8_ascii_progression);
  RUN_TEST(test_utf8_multibyte_codepoint);
  RUN_TEST(test_utf8_four_byte_codepoint);
  RUN_TEST(test_utf8_invalid_falls_back_to_single_byte);
  return UNITY_END();
}
