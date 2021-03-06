// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

#include "ccomptr.h"
#include "common_action_mocks.h"
#include "cor_debug_helper.h"
#include "dbg_array.h"
#include "dbg_object_factory.h"
#include "i_cor_debug_mocks.h"
#include "i_eval_coordinator_mock.h"
#include "variable_wrapper.h"

using google::cloud::diagnostics::debug::Variable;
using google_cloud_debugger::CComPtr;
using google_cloud_debugger::CorDebugHelper;
using google_cloud_debugger::DbgArray;
using google_cloud_debugger::DbgObjectFactory;
using google_cloud_debugger::ICorDebugHelper;
using google_cloud_debugger::IDbgObjectFactory;
using google_cloud_debugger::VariableWrapper;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArrayArgument;

namespace google_cloud_debugger_test {

// Test Fixture for DbgArray.
// Contains various ICorDebug mock objects needed.
class DbgArrayTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // By default, sets array_value_ to the second argument
    // whenever QueryInterface is called.
    ON_CALL(array_value_, QueryInterface(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(&array_value_), Return(S_OK)));

    debug_helper_ = std::shared_ptr<ICorDebugHelper>(new CorDebugHelper());
    dbg_object_factory_ =
        std::shared_ptr<IDbgObjectFactory>(new DbgObjectFactory());
  }

  // Sets up various mock objects so when we use them with
  // a DbgArray class, we will get an array with 2 elements.
  void SetUpArray() {
    EXPECT_CALL(array_type_, GetFirstTypeParameter(_))
        .WillRepeatedly(
            DoAll(SetArgPointee<0>(&array_element_type_), Return(S_OK)));

    EXPECT_CALL(array_element_type_, GetType(_))
        .WillRepeatedly(DoAll(SetArgPointee<0>(CorElementType::ELEMENT_TYPE_I4),
                              Return(S_OK)));

    // If queried for ICorDebugHeapValue2, returns heap_value.
    // This happens when the Initialize function tries to create a strong
    // handle of the array.
    ON_CALL(array_value_, QueryInterface(__uuidof(ICorDebugHeapValue2), _))
        .WillByDefault(DoAll(SetArgPointee<1>(&heap_value_), Return(S_OK)));

    // Makes heap_value returns handle_value if CreateHandle is called.
    EXPECT_CALL(heap_value_, CreateHandle(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(&handle_value_), Return(S_OK)));

    // The handle should dereference to the array value.
    ON_CALL(handle_value_, Dereference(_))
        .WillByDefault(DoAll(SetArgPointee<0>(&array_value_), Return(S_OK)));

    // Initialize function should issue call to get dimensions
    // and ranks of the array.
    EXPECT_CALL(array_value_, GetDimensions(_, _))
        .WillRepeatedly(DoAll(SetArrayArgument<1>(dimensions_, dimensions_ + 1),
                              Return(S_OK)));

    EXPECT_CALL(array_type_, GetRank(_))
        .WillRepeatedly(DoAll(SetArgPointee<0>(1), Return(S_OK)));
  }

  // An array with 2 elements.
  ULONG32 dimensions_[1] = {2};

  // ICorDebugHelper used for array constructor.
  std::shared_ptr<ICorDebugHelper> debug_helper_;

  // IDbgObjectFactory used for array constructor.
  std::shared_ptr<IDbgObjectFactory> dbg_object_factory_;

  // Types of the array.
  ICorDebugTypeMock array_type_;
  ICorDebugTypeMock array_element_type_;

  // ICorDebugValue that represents the array.
  ICorDebugArrayValueMock array_value_;

  // Heap and handle value created for the array.
  ICorDebugHeapValue2Mock heap_value_;
  ICorDebugHandleValueMock handle_value_;

  // EvalCoordinator to evaluate array members.
  IEvalCoordinatorMock eval_coordinator_;
};

// Tests Initialize function of DbgArray.
TEST_F(DbgArrayTest, Initialize) {
  SetUpArray();

  // Have to make sure that the mock value survives after
  // the destructor of DbgArray is called, otherwise, DbgArray
  // may try to delete value that is obselete.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    dbgarray.Initialize(&array_value_, FALSE);
    HRESULT hr = dbgarray.GetInitializeHr();
    EXPECT_TRUE(SUCCEEDED(hr)) << "Failed with hr: " << hr;
  }
}

// Tests error cases for Initialize function of DbgArray.
TEST_F(DbgArrayTest, InitializeError) {
  // Null type.
  {
    DbgArray dbgarray(nullptr, 1, debug_helper_, dbg_object_factory_);
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), E_INVALIDARG);
  }

  // Makes GetFirstTypeParameter returns error.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    EXPECT_CALL(array_type_, GetFirstTypeParameter(_))
        .Times(1)
        .WillRepeatedly(Return(CORDBG_E_CONTEXT_UNVAILABLE));
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), CORDBG_E_CONTEXT_UNVAILABLE);
  }

  EXPECT_CALL(array_type_, GetFirstTypeParameter(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(&array_element_type_), Return(S_OK)));

  // Returns failure when querying the element type of the array.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    EXPECT_CALL(array_element_type_, GetType(_))
        .WillRepeatedly(Return(E_ACCESSDENIED));
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), E_ACCESSDENIED);
  }

  EXPECT_CALL(array_element_type_, GetType(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(CorElementType::ELEMENT_TYPE_I4),
                            Return(S_OK)));

  // Makes GetRank returns error.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    EXPECT_CALL(array_type_, GetRank(_))
        .WillRepeatedly(
            DoAll(SetArgPointee<0>(1), Return(COR_E_SAFEARRAYRANKMISMATCH)));
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), COR_E_SAFEARRAYRANKMISMATCH);
  }

  EXPECT_CALL(array_type_, GetRank(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(1), Return(S_OK)));

  // By default, sets array_value_ to the second argument
  // whenever QueryInterface is called.
  ON_CALL(array_value_, QueryInterface(_, _))
      .WillByDefault(DoAll(SetArgPointee<1>(&array_value_), Return(S_OK)));

  // Returns error when trying to create a handle for the array.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    ON_CALL(array_value_, QueryInterface(__uuidof(ICorDebugHeapValue2), _))
        .WillByDefault(Return(E_NOINTERFACE));
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), E_NOINTERFACE);
  }

  ON_CALL(array_value_, QueryInterface(__uuidof(ICorDebugHeapValue2), _))
      .WillByDefault(DoAll(SetArgPointee<1>(&heap_value_), Return(S_OK)));

  // Returns error when trying to create a handle for the array.
  {
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    EXPECT_CALL(heap_value_, CreateHandle(_, _))
        .Times(1)
        .WillRepeatedly(Return(CORDBG_E_BAD_REFERENCE_VALUE));
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), CORDBG_E_BAD_REFERENCE_VALUE);
  }

  // Makes heap_value returns handle_value if CreateHandle is called.
  EXPECT_CALL(heap_value_, CreateHandle(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(&handle_value_), Return(S_OK)));

  // Makes GetDimensions returns incorrect value.
  {
    EXPECT_CALL(array_value_, GetDimensions(_, _))
        .Times(1)
        .WillRepeatedly(Return(CORDBG_S_BAD_START_SEQUENCE_POINT));
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
    dbgarray.Initialize(&array_value_, FALSE);
    EXPECT_EQ(dbgarray.GetInitializeHr(), CORDBG_S_BAD_START_SEQUENCE_POINT);
  }
}

// Tests GetArrayItem function of DbgArray.
TEST_F(DbgArrayTest, TestGetArrayItem) {
  SetUpArray();

  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
  dbgarray.Initialize(&array_value_, FALSE);
  HRESULT hr = dbgarray.GetInitializeHr();
  EXPECT_TRUE(SUCCEEDED(hr)) << "Failed with hr: " << hr;

  int position = 3;
  CComPtr<ICorDebugValue> array_item;

  EXPECT_CALL(array_value_, GetElementAtPosition(position, _))
      .Times(1)
      .WillRepeatedly(Return(S_OK));
  hr = dbgarray.GetArrayItem(position, &array_item);
  EXPECT_TRUE(SUCCEEDED(hr)) << "Failed with hr: " << hr;
}

// Tests error cases for GetArrayItem function of DbgArray.
TEST_F(DbgArrayTest, TestGetArrayItemError) {
  SetUpArray();

  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
  int position = 3;
  CComPtr<ICorDebugValue> array_item;

  // If array is not initialized, error should be thrown.
  { EXPECT_EQ(dbgarray.GetArrayItem(position, &array_item), E_FAIL); }

  dbgarray.Initialize(&array_value_, FALSE);

  EXPECT_CALL(array_value_, GetElementAtPosition(position, _))
      .Times(1)
      .WillRepeatedly(Return(E_ACCESSDENIED));
  EXPECT_EQ(dbgarray.GetArrayItem(position, &array_item), E_ACCESSDENIED);
}

// Tests PopulateType function of DbgArray.
TEST_F(DbgArrayTest, TestPopulateType) {
  SetUpArray();

  Variable variable;
  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
  dbgarray.Initialize(&array_value_, FALSE);

  dbgarray.PopulateType(&variable);
  EXPECT_EQ(variable.type(), "System.Int32[]");
}

// Tests PopulateType function of DbgArray.
TEST_F(DbgArrayTest, TestPopulateTypeError) {
  SetUpArray();

  {
    Variable variable;
    // Since the type given is null, Initialize function will return
    // E_INVALIDARG.
    DbgArray dbgarray(nullptr, 1, debug_helper_, dbg_object_factory_);
    dbgarray.Initialize(&array_value_, FALSE);

    // PopulateType should return E_INVALIDARG since Initialize
    // function fails.
    EXPECT_EQ(dbgarray.GetInitializeHr(), E_INVALIDARG);
    EXPECT_EQ(dbgarray.GetInitializeHr(), dbgarray.PopulateType(&variable));
  }

  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
  dbgarray.Initialize(&array_value_, FALSE);

  // Should throws error for null variable.
  EXPECT_EQ(dbgarray.PopulateType(nullptr), E_INVALIDARG);
}

// Tests PopulateMembers function of DbgArray.
TEST_F(DbgArrayTest, TestPopulateMembers) {
  SetUpArray();

  // If array is null, then variables should have 0 members.
  {
    Variable variable;
    vector<VariableWrapper> variable_wrappers;
    DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);

    // Initialize to a null array.
    dbgarray.Initialize(&array_value_, TRUE);
    HRESULT hr = dbgarray.PopulateMembers(&variable, &variable_wrappers,
                                          &eval_coordinator_);
    EXPECT_TRUE(SUCCEEDED(hr)) << "Failed with hr: " << hr;

    EXPECT_EQ(variable.members_size(), 0);
    EXPECT_EQ(variable_wrappers.size(), 0);
  }

  Variable variable;
  vector<VariableWrapper> variable_wrappers;
  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);

  dbgarray.Initialize(&array_value_, FALSE);

  ICorDebugGenericValueMock item0;
  int32_t value0 = 20;
  SetUpMockGenericValue(&item0, value0);

  // Returns ICorDebugValue that represents 20.
  EXPECT_CALL(array_value_, GetElementAtPosition(0, _))
      .Times(1)
      .WillRepeatedly(DoAll(SetArgPointee<1>(&item0), Return(S_OK)));

  ICorDebugGenericValueMock item1;
  int32_t value1 = 40;
  SetUpMockGenericValue(&item1, value1);

  // Returns ICorDebugValue that represents 40.
  EXPECT_CALL(array_value_, GetElementAtPosition(1, _))
      .Times(1)
      .WillRepeatedly(DoAll(SetArgPointee<1>(&item1), Return(S_OK)));

  HRESULT hr = dbgarray.PopulateMembers(&variable, &variable_wrappers,
                                        &eval_coordinator_);
  EXPECT_TRUE(SUCCEEDED(hr)) << "Failed with hr: " << hr;

  // Checks that the variable proto in the wrapper is children
  // of the proto we passed in to PopulateMembers.
  EXPECT_EQ(variable_wrappers.size(), 2);
  EXPECT_EQ(variable_wrappers[0].GetVariableProto(), &(variable.members(0)));
  EXPECT_EQ(variable_wrappers[1].GetVariableProto(), &(variable.members(1)));

  PopulateTypeAndValue(variable_wrappers);

  // Checks type and value.
  EXPECT_EQ(variable.members(0).type(), "System.Int32");
  EXPECT_EQ(variable.members(1).type(), "System.Int32");

  EXPECT_EQ(variable.members(0).value(), std::to_string(value0));
  EXPECT_EQ(variable.members(1).value(), std::to_string(value1));
}

// Tests error case for PopulateMembers function of DbgArray.
TEST_F(DbgArrayTest, TestPopulateMembersError) {
  SetUpArray();

  // If Initialize is not called.
  {
    // Since the type given is null, Initialize function will return
    // E_INVALIDARG.
    DbgArray dbgarray(nullptr, 1, debug_helper_, dbg_object_factory_);
    dbgarray.Initialize(&array_value_, FALSE);

    // PopulateMembers should return E_INVALIDARG since Initialize
    // function fails.
    EXPECT_EQ(dbgarray.GetInitializeHr(), E_INVALIDARG);
    Variable variable;
    vector<VariableWrapper> variable_wrappers;
    EXPECT_EQ(dbgarray.GetInitializeHr(),
              dbgarray.PopulateMembers(&variable, &variable_wrappers,
                                       &eval_coordinator_));
  }

  DbgArray dbgarray(&array_type_, 1, debug_helper_, dbg_object_factory_);
  dbgarray.Initialize(&array_value_, FALSE);

  // Should throws error for null variable.
  vector<VariableWrapper> variable_wrappers;
  EXPECT_EQ(
      dbgarray.PopulateMembers(nullptr, &variable_wrappers, &eval_coordinator_),
      E_INVALIDARG);

  Variable variable;
  // Should throws error for null variable wrappers vector.
  EXPECT_EQ(dbgarray.PopulateMembers(&variable, nullptr, &eval_coordinator_),
            E_INVALIDARG);

  // Should throws error for null eval coordinator.
  EXPECT_EQ(dbgarray.PopulateMembers(&variable, &variable_wrappers, nullptr),
            E_INVALIDARG);
}

}  // namespace google_cloud_debugger_test
