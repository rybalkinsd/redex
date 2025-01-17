/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <array>
#include <gtest/gtest.h>

#include "Debug.h"
#include "RedexResources.h"
#include "androidfw/ResourceTypes.h"

std::string make_big_string(size_t len) {
  always_assert(len > 4);
  std::string result = "aa" + std::string(len - 4, 'x') + "zz";
  return result;
}

void assert_u16_string(std::u16string actual_str, std::string expected) {
  std::u16string expected_str(expected.begin(), expected.end());
  ASSERT_EQ(actual_str, expected_str);
}

TEST(ResStringPool, AppendToEmptyTable) {
  const size_t header_size = sizeof(android::ResStringPool_header);
  android::ResStringPool_header header = {
      {htods(android::RES_STRING_POOL_TYPE),
       htods(header_size),
       htodl(header_size)},
      0,
      0,
      htodl(android::ResStringPool_header::UTF8_FLAG |
            android::ResStringPool_header::SORTED_FLAG),
      0,
      0};
  android::ResStringPool pool((void*)&header, header_size, false);

  pool.appendString(android::String8("Hello, world"));
  auto big_string = make_big_string(300);
  auto big_chars = big_string.c_str();
  pool.appendString(android::String8(big_chars));
  pool.appendString(android::String8("€666"));
  pool.appendString(android::String8("banana banana"));
  android::Vector<char> v;
  pool.serialize(v);

  auto data = (void*)v.array();
  android::ResStringPool after(data, v.size(), false);

  // Ensure sort bit was cleared.
  auto flags = dtohl(((android::ResStringPool_header*)data)->flags);
  ASSERT_FALSE(flags & android::ResStringPool_header::SORTED_FLAG);

  size_t out_len;
  ASSERT_STREQ(after.string8At(0, &out_len), "Hello, world");
  ASSERT_EQ(out_len, 12);
  ASSERT_STREQ(after.string8At(1, &out_len), big_chars);
  ASSERT_EQ(out_len, 300);
  ASSERT_STREQ(after.string8At(2, &out_len), "€666");
  ASSERT_STREQ(after.string8At(3, &out_len), "banana banana");
}

TEST(ResStringPool, AppendToExistingUTF8) {
  // Chunk of just the ResStringPool, as generated by aapt2 (has 2 UTF8 strings)
  const std::array<uint8_t, 84> data{{
      0x01, 0x00, 0x1C, 0x00, 0x54, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
      0x0C, 0x0C, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x77, 0x6F, 0x72,
      0x6C, 0x64, 0x00, 0x1C, 0x1C, 0x72, 0x65, 0x73, 0x2F, 0x6C, 0x61, 0x79,
      0x6F, 0x75, 0x74, 0x2F, 0x73, 0x69, 0x6D, 0x70, 0x6C, 0x65, 0x5F, 0x6C,
      0x61, 0x79, 0x6F, 0x75, 0x74, 0x2E, 0x78, 0x6D, 0x6C, 0x00, 0x00, 0x00}};
  android::ResStringPool pool(&data, data.size(), false);
  size_t out_len;
  ASSERT_STREQ(pool.string8At(0, &out_len), "Hello, world");

  pool.appendString(android::String8("this is another string"));
  android::Vector<char> v;
  pool.serialize(v);
  android::ResStringPool after((void*)v.array(), v.size(), false);

  // Make sure we still have the original two strings
  ASSERT_STREQ(after.string8At(0, &out_len), "Hello, world");
  ASSERT_EQ(out_len, 12);
  ASSERT_STREQ(after.string8At(1, &out_len), "res/layout/simple_layout.xml");
  ASSERT_EQ(out_len, 28);
  // And the one appended
  ASSERT_STREQ(after.string8At(2, &out_len), "this is another string");
  ASSERT_EQ(out_len, 22);
}

TEST(ResStringPool, AppendToExistingUTF16) {
  const std::array<uint8_t, 116> data{{
      0x01, 0x00, 0x1C, 0x00, 0x74, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
      0x1C, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
      0x05, 0x00, 0x63, 0x00, 0x6F, 0x00, 0x6C, 0x00, 0x6F, 0x00, 0x72, 0x00,
      0x00, 0x00, 0x05, 0x00, 0x64, 0x00, 0x69, 0x00, 0x6D, 0x00, 0x65, 0x00,
      0x6E, 0x00, 0x00, 0x00, 0x02, 0x00, 0x69, 0x00, 0x64, 0x00, 0x00, 0x00,
      0x06, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x79, 0x00, 0x6F, 0x00, 0x75, 0x00,
      0x74, 0x00, 0x00, 0x00, 0x06, 0x00, 0x73, 0x00, 0x74, 0x00, 0x72, 0x00,
      0x69, 0x00, 0x6E, 0x00, 0x67, 0x00, 0x00, 0x00}};
  android::ResStringPool pool(&data, data.size(), false);
  ASSERT_TRUE(!pool.isUTF8());
  size_t out_len;
  auto s = pool.stringAt(0, &out_len);
  assert_u16_string(s, "color");
  ASSERT_EQ(out_len, 5);

  // Make sure the size encoding works for large values.
  auto big_string = make_big_string(35000);
  auto big_chars = big_string.c_str();
  pool.appendString(android::String8(big_chars));
  pool.appendString(android::String8("more more more"));
  android::Vector<char> v;
  pool.serialize(v);
  android::ResStringPool after((void*)v.array(), v.size(), false);

  assert_u16_string(after.stringAt(0, &out_len), "color");
  ASSERT_EQ(out_len, 5);
  assert_u16_string(after.stringAt(1, &out_len), "dimen");
  ASSERT_EQ(out_len, 5);
  assert_u16_string(after.stringAt(2, &out_len), "id");
  ASSERT_EQ(out_len, 2);
  assert_u16_string(after.stringAt(3, &out_len), "layout");
  ASSERT_EQ(out_len, 6);
  assert_u16_string(after.stringAt(4, &out_len), "string");
  ASSERT_EQ(out_len, 6);
  assert_u16_string(after.stringAt(5, &out_len), big_chars);
  ASSERT_EQ(out_len, 35000);
  assert_u16_string(after.stringAt(6, &out_len), "more more more");
  ASSERT_EQ(out_len, 14);
}

TEST(ResStringPool, ReplaceStringsInXmlLayout) {
  // Given layout file should have a series of View subclasses in the XML, which
  // we will rename. Parse the resulting binary data, and make sure all tags are
  // right.
  size_t length;
  int file_descriptor;
  auto fp =
      map_file(std::getenv("test_layout_path"), &file_descriptor, &length);

  std::map<std::string, std::string> shortened_names;
  shortened_names.emplace("com.example.test.CustomViewGroup", "Z.a");
  shortened_names.emplace("com.example.test.CustomTextView", "Z.b");
  shortened_names.emplace("com.example.test.CustomButton", "Z.c");
  shortened_names.emplace("com.example.test.NotFound", "Z.d");

  android::Vector<char> serialized;
  size_t num_renamed = 0;
  replace_in_xml_string_pool(
    fp,
    length,
    shortened_names,
    &serialized,
    &num_renamed);

  EXPECT_EQ(num_renamed, 3);
  android::ResXMLTree parser;
  parser.setTo(&serialized[0], serialized.size());
  EXPECT_EQ(android::NO_ERROR, parser.getError())
    << "Error parsing layout after rename";

  std::vector<std::string> expected_xml_tags;
  expected_xml_tags.push_back("Z.a");
  expected_xml_tags.push_back("TextView");
  expected_xml_tags.push_back("Z.b");
  expected_xml_tags.push_back("Z.c");
  expected_xml_tags.push_back("Button");

  size_t tag_count = 0;
  android::ResXMLParser::event_code_t type;
  do {
    type = parser.next();
    if (type == android::ResXMLParser::START_TAG) {
      EXPECT_LT(tag_count, 5);
      size_t len;
      android::String8 tag(parser.getElementName(&len));
      auto actual_chars = tag.string();
      auto expected_chars = expected_xml_tags[tag_count].c_str();
      EXPECT_STREQ(actual_chars, expected_chars);
      tag_count++;
    }
  } while (type != android::ResXMLParser::BAD_DOCUMENT &&
           type != android::ResXMLParser::END_DOCUMENT);
  EXPECT_EQ(tag_count, 5);

  unmap_and_close(file_descriptor, fp, length);
}

void assert_serialized_data(void* original, size_t length, android::Vector<char>& serialized) {
  ASSERT_EQ(length, serialized.size());
  for (size_t i = 0; i < length; i++) {
    auto actual = *((char*) original + i);
    ASSERT_EQ(actual, serialized[i]);
  }
}

TEST(ResTable, TestRoundTrip) {
  size_t length;
  int file_descriptor;
  auto fp = map_file(std::getenv("test_arsc_path"), &file_descriptor, &length);
  android::ResTable table;
  ASSERT_EQ(table.add(fp, length), 0);
  // Just invoke the serialize method to ensure the same data comes back
  android::Vector<char> serialized;
  table.serialize(serialized, 0);
  assert_serialized_data(fp, length, serialized);
  unmap_and_close(file_descriptor, fp, length);
}

TEST(ResTable, AppendNewType) {
  size_t length;
  int file_descriptor;
  auto fp = map_file(std::getenv("test_arsc_path"), &file_descriptor, &length);
  android::ResTable table;
  ASSERT_EQ(table.add(fp, length), 0);
  // Read the number of original types.
  android::Vector<android::String8> original_type_names;
  table.getTypeNamesForPackage(0, &original_type_names);

  // Copy some existing entries to a different table, verify serialization
  const uint8_t dest_type = 3;
  android::Vector<uint32_t> source_ids;
  source_ids.push_back(0x7f010000);
  size_t num_ids = source_ids.size();
  android::Vector<android::Res_value> values;
  for (size_t i = 0; i < num_ids; i++) {
    android::Res_value val;
    table.getResource(source_ids[i], &val);
    values.push_back(val);
  }

  android::ResTable_config config = {
    sizeof(android::ResTable_config)
  };

  android::Vector<android::ResTable_config> config_vec;
  config_vec.push(config);
  table.defineNewType(
    android::String8("foo"),
    dest_type,
    config_vec,
    source_ids);

  android::Vector<char> serialized;
  table.serialize(serialized, 0);

  android::ResTable round_trip;
  ASSERT_EQ(round_trip.add((void*)serialized.array(), serialized.size()), 0);
  // Make sure entries exist in 0x7f03xxxx range
  for (size_t i = 0; i < num_ids; i++) {
    auto old_id = source_ids[i];
    auto new_id = 0x7f000000 | (dest_type << 16) | (old_id & 0xFFFF);
    android::Res_value expected = values[i];
    android::Res_value actual;
    round_trip.getResource(new_id, &actual);
    ASSERT_EQ(expected.dataType, actual.dataType);
    ASSERT_EQ(expected.data, actual.data);
  }

  // Sanity check values in their original location
  {
    android::Res_value out_value;
    round_trip.getResource(0x7f010000, &out_value);
    float val = android::complex_value(out_value.data);
    uint32_t unit = android::complex_unit(out_value.data, false);
    ASSERT_EQ((int) val, 10);
    ASSERT_EQ(unit, android::Res_value::COMPLEX_UNIT_DIP);
  }
  {
    android::Res_value out_value;
    round_trip.getResource(0x7f010001, &out_value);
    float val = android::complex_value(out_value.data);
    uint32_t unit = android::complex_unit(out_value.data, false);
    ASSERT_EQ((int) val, 20);
    ASSERT_EQ(unit, android::Res_value::COMPLEX_UNIT_DIP);
  }

  android::Vector<android::String8> type_names;
  round_trip.getTypeNamesForPackage(0, &type_names);
  ASSERT_EQ(type_names.size(), original_type_names.size() + 1);

  unmap_and_close(file_descriptor, fp, length);
}
