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

#include "dbg_class_field.h"

#include <iostream>

#include "breakpoint.pb.h"
#include "constants.h"
#include "i_cor_debug_helper.h"
#include "i_eval_coordinator.h"

using google::cloud::diagnostics::debug::Variable;
using std::string;

namespace google_cloud_debugger {
void DbgClassField::Initialize(mdToken field_def,
                               IMetaDataImport *metadata_import) {
  // If a field is a backing field of a property, its name will
  // end with this.
  static const string kBackingField = ">k__BackingField";

  if (metadata_import == nullptr) {
    WriteError("MetaDataImport is null.");
    initialized_hr_ = E_INVALIDARG;
    return;
  }

  CComPtr<ICorDebugValue> field_value;
  ULONG len_field_name;

  field_def_ = field_def;

  // First call to get length of array.
  initialized_hr_ = metadata_import->GetFieldProps(
      field_def_, &class_token_, nullptr, 0, &len_field_name,
      &field_attributes_, &signature_metadata_, &signature_metadata_len_,
      &default_value_type_flags_, &default_value_, &default_value_len_);
  if (FAILED(initialized_hr_)) {
    WriteError("Failed to populate field metadata.");
    return;
  }

  std::vector<WCHAR> wchar_field_name(len_field_name, 0);

  // Second call to get the actual name.
  initialized_hr_ = metadata_import->GetFieldProps(
      field_def_, &class_token_, wchar_field_name.data(), len_field_name,
      &len_field_name, &field_attributes_, &signature_metadata_,
      &signature_metadata_len_, &default_value_type_flags_, &default_value_,
      &default_value_len_);
  if (FAILED(initialized_hr_)) {
    WriteError("Failed to populate field metadata.");
    return;
  }

  field_name_ = ConvertWCharPtrToString(wchar_field_name);

  // If field name is <MyProperty>k__BackingField, change it to
  // MyProperty because it is the backing field of a property.
  if (field_name_.size() > kBackingField.size() + 1) {
    // Checks that field name is of the form <Property>k__BackingField.
    if (field_name_[0] == '<') {
      string::size_type position;
      // Checks that field_name_ ends with k_BackingField.
      position = field_name_.find(kBackingField,
                                  field_name_.size() - kBackingField.size());
      // Extracts out the field name.
      if (position != string::npos) {
        is_backing_field_ = true;
        field_name_ = field_name_.substr(1, position - 1);
      }
    }
  }
}

HRESULT DbgClassField::PopulateVariableValue(
    google::cloud::diagnostics::debug::Variable *variable,
    ICorDebugReferenceValue *reference_value,
    IEvalCoordinator *eval_coordinator,
    std::vector<CComPtr<ICorDebugType>> *generic_types, int depth) {
  if (FAILED(initialized_hr_)) {
    return initialized_hr_;
  }

  if (!variable || !eval_coordinator) {
    return E_INVALIDARG;
  }

  HRESULT hr;

  // In case field_value_ is cached, sets the evaluation depth again.
  if (!field_value_) {
    CComPtr<ICorDebugValue> dereferenced_value;
    BOOL is_null;

    HRESULT hr = Dereference(reference_value, &dereferenced_value, &is_null,
                             GetErrorStream());
    if (FAILED(hr)) {
      WriteError("Failed to dereference class value.");
      return hr;
    }

    if (IsStatic()) {
      hr = ExtractStaticFieldValue(dereferenced_value, eval_coordinator);
      if (FAILED(hr)) {
        WriteError("Failed to extract static field value.");
        return hr;
      }
    } else {
      // Non-Static field needs a non-null object.
      if (is_null) {
        WriteError(
            "Cannot get non-static field value since class object is null.");
        return hr;
      }

      hr = ExtractNonStaticFieldValue(dereferenced_value, depth);
      if (FAILED(hr)) {
        WriteError("Failed to extract non-static field value.");
        return hr;
      }
    }
  }

  if (!field_value_) {
    WriteError("Cannot get field value.");
    return E_FAIL;
  }

  // In case field_value_ is cached, sets the evaluation depth again.
  field_value_->SetEvaluationDepth(depth);
  hr = field_value_->PopulateVariableValue(variable, eval_coordinator);
  if (FAILED(hr)) {
    WriteError(field_value_->GetErrorString());
  }

  return hr;
}

HRESULT DbgClassField::ExtractStaticFieldValue(
    ICorDebugValue *class_value, IEvalCoordinator *eval_coordinator) {
  CComPtr<ICorDebugType> debug_type;
  HRESULT hr = GetICorDebugType(class_value, &debug_type);
  if (FAILED(hr)) {
    WriteError("Failed to get ICorDebugType.");
    return hr;
  }

  if (!debug_type) {
    WriteError("Cannot evaluate static field without ICorDebugType.");
    return E_FAIL;
  }

  CComPtr<ICorDebugThread> active_thread;
  hr = eval_coordinator->GetActiveDebugThread(&active_thread);
  if (FAILED(hr)) {
    WriteError("Failed to get active debug thread.");
    return hr;
  }

  CComPtr<ICorDebugFrame> debug_frame;
  hr = active_thread->GetActiveFrame(&debug_frame);
  if (FAILED(hr)) {
    WriteError("Failed to get the active frame.");
    return hr;
  }

  CComPtr<ICorDebugValue> debug_value;
  hr = debug_type->GetStaticFieldValue(field_def_, debug_frame, &debug_value);
  if (hr == CORDBG_E_STATIC_VAR_NOT_AVAILABLE) {
    WriteError("Static variable is not yet available.");
    return hr;
  }

  // This error should only be applicable to C++?
  if (hr == CORDBG_E_VARIABLE_IS_ACTUALLY_LITERAL) {
    WriteError("Static variable is literal.");
    return hr;
  }

  if (FAILED(hr)) {
    WriteError("Failed to get static field value.");
    return hr;
  }

  // BUG: String that starts with @ cannot be retrieved.
  // For static field, use default evaluation depth.
  hr = DbgObject::CreateDbgObject(debug_value, kDefaultObjectEvalDepth,
                                  &field_value_, GetErrorStream());
  if (FAILED(hr)) {
    if (field_value_) {
      WriteError(field_value_->GetErrorString());
    }
    WriteError("Failed to create DbgObject for static field value.");
    field_value_.release();
    return hr;
  }

  return hr;
}

HRESULT DbgClassField::ExtractNonStaticFieldValue(ICorDebugValue *class_value,
                                                  int depth) {
  CComPtr<ICorDebugObjectValue> object_value;

  HRESULT hr = class_value->QueryInterface(
      __uuidof(ICorDebugObjectValue), reinterpret_cast<void **>(&object_value));
  if (FAILED(hr)) {
    WriteError("Failed to cast class objecrt to ICorDebugObjectValue.");
    return hr;
  }

  CComPtr<ICorDebugClass> debug_class;
  hr = object_value->GetClass(&debug_class);
  if (FAILED(hr)) {
    WriteError("Failed to get class from object value.");
    return hr;
  }

  CComPtr<ICorDebugValue> dbg_field_value;
  hr = object_value->GetFieldValue(debug_class, field_def_, &dbg_field_value);
  if (hr == CORDBG_E_FIELD_NOT_AVAILABLE) {
    WriteError("Field is optimized away");
    return hr;
  }

  if (initialized_hr_ == CORDBG_E_CLASS_NOT_LOADED) {
    WriteError("Class of the field is not loaded.");
    return hr;
  }

  if (initialized_hr_ == CORDBG_E_VARIABLE_IS_ACTUALLY_LITERAL) {
    WriteError(
        "Field is a literal. It is optimized away and is not available.");
    return hr;
  }

  if (FAILED(hr)) {
    WriteError("Failed to get field value.");
    return hr;
  }

  hr = DbgObject::CreateDbgObject(dbg_field_value, depth, &field_value_,
                                  GetErrorStream());

  if (FAILED(hr)) {
    WriteError("Failed to create DbgObject for field.");
    if (field_value_) {
      WriteError(field_value_->GetErrorString());
    }
  }

  return hr;
}

}  // namespace google_cloud_debugger
