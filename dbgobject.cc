// Copyright 2015-2016 Google Inc. All Rights Reserved.
// Licensed under the Apache License Version 2.0.

#include "dbgobject.h"

#include <cstdint>
#include <iostream>

#include "dbgarray.h"
#include "dbgclass.h"
#include "dbgprimitive.h"
#include "dbgstring.h"
#include "evalcoordinator.h"

namespace google_cloud_debugger {
using std::unique_ptr;
using std::cerr;
using std::cout;

DbgObject::DbgObject(ICorDebugType *debug_type, int depth) {
  debug_type_ = debug_type;
  depth_ = depth;
}

HRESULT DbgObject::CreateDbgObjectHelper(
    ICorDebugValue *debug_value, ICorDebugType *debug_type,
    CorElementType cor_element_type, BOOL is_null, int depth,
    std::unique_ptr<DbgObject> *result_object) {
  if (!result_object) {
    cerr << "result_object cannot be a null pointer";
    return E_INVALIDARG;
  }

  unique_ptr<DbgObject> temp_object;
  switch (cor_element_type) {
    case CorElementType::ELEMENT_TYPE_BOOLEAN:
      temp_object = unique_ptr<DbgObject>(new (std::nothrow)
                                              DbgPrimitive<bool>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_CHAR:
      temp_object = unique_ptr<DbgObject>(new (std::nothrow)
                                              DbgPrimitive<char>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_I:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<intptr_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_U:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<uintptr_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_I1:
      temp_object = unique_ptr<DbgObject>(new (std::nothrow)
                                              DbgPrimitive<int8_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_U1:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<uint8_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_I2:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<int16_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_U2:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<uint16_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_I4:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<int32_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_U4:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<uint32_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_I8:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<int64_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_U8:
      temp_object = unique_ptr<DbgObject>(
          new (std::nothrow) DbgPrimitive<uint64_t>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_R4:
      temp_object = unique_ptr<DbgObject>(new (std::nothrow)
                                              DbgPrimitive<float>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_R8:
      temp_object = unique_ptr<DbgObject>(new (std::nothrow)
                                              DbgPrimitive<double>(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_STRING:
      temp_object =
          unique_ptr<DbgObject>(new (std::nothrow) DbgString(debug_type));
      break;
    case CorElementType::ELEMENT_TYPE_SZARRAY:
    case CorElementType::ELEMENT_TYPE_ARRAY:
      temp_object =
          unique_ptr<DbgObject>(new (std::nothrow) DbgArray(debug_type, depth));
      break;
    case CorElementType::ELEMENT_TYPE_CLASS:
    case CorElementType::ELEMENT_TYPE_VALUETYPE:
      temp_object =
          unique_ptr<DbgObject>(new (std::nothrow) DbgClass(debug_type, depth));
      break;
    default:
      return E_NOTIMPL;
  }

  if (temp_object) {
    HRESULT hr = temp_object->Initialize(debug_value, is_null);
    if (FAILED(hr)) {
      temp_object = nullptr;
      cerr << "Failed to call initialize on DbgObject: " << std::hex << hr;
      return hr;
    }
  } else {
    cerr << "Failed to create DbgObject.";
    return E_OUTOFMEMORY;
  }

  (*result_object) = std::move(temp_object);
  return S_OK;
}

HRESULT DbgObject::CreateDbgObject(ICorDebugType *debug_type,
                                   unique_ptr<DbgObject> *result_object) {
  CorElementType cor_element_type;
  HRESULT hr;

  hr = debug_type->GetType(&cor_element_type);
  if (FAILED(hr)) {
    cerr << "Failed to get type: " << std::hex << hr;
    return hr;
  }

  return DbgObject::CreateDbgObjectHelper(nullptr, debug_type, cor_element_type,
                                          TRUE, 0, result_object);
}

HRESULT DbgObject::CreateDbgObject(ICorDebugValue *debug_value, int depth,
                                   unique_ptr<DbgObject> *result_object) {
  HRESULT hr;
  BOOL is_null = FALSE;
  CComPtr<ICorDebugValue> dereferenced_and_unboxed_value;
  CComPtr<ICorDebugValue2> debug_value_2;
  CComPtr<ICorDebugType> debug_type;
  CorElementType cor_element_type;

  hr = DereferenceAndUnbox(debug_value, &dereferenced_and_unboxed_value,
                           &is_null);
  if (FAILED(hr)) {
    cerr << "Failed to dereference and unbox.";
    return hr;
  }

  hr = dereferenced_and_unboxed_value->QueryInterface(
      __uuidof(ICorDebugValue2), reinterpret_cast<void **>(&debug_value_2));

  if (SUCCEEDED(hr)) {
    hr = debug_value_2->GetExactType(&debug_type);
    if (SUCCEEDED(hr)) {
      hr = debug_type->GetType(&cor_element_type);
    }
  } else if (hr == E_NOINTERFACE) {
    hr = debug_value->GetType(&cor_element_type);
  }

  if (FAILED(hr)) {
    // Nothing we can do here.
    cerr << "Failed to get type.";
    return hr;
  }

  return CreateDbgObjectHelper(dereferenced_and_unboxed_value, debug_type,
                               cor_element_type, is_null, depth, result_object);
}

HRESULT Dereference(ICorDebugValue *debug_value,
                    ICorDebugValue **dereferenced_value, BOOL *is_null) {
  if (!dereferenced_value) {
    cerr << "dereferenced_value cannot be a null pointer.";
    return E_INVALIDARG;
  }

  BOOL local_is_null = FALSE;
  HRESULT hr;
  int reference_depth = 0;
  CComPtr<ICorDebugValue> temp_value;
  temp_value = debug_value;

  while (reference_depth < kReferenceDepth) {
    CComPtr<ICorDebugReferenceValue> debug_reference;

    hr =
        temp_value->QueryInterface(__uuidof(ICorDebugReferenceValue),
                                   reinterpret_cast<void **>(&debug_reference));

    // If not a reference value, don't do anything.
    if (hr == E_NOINTERFACE) {
      *is_null = FALSE;
      break;
    } else if (FAILED(hr)) {
      cerr << "Failed to convert ICorDebugValue to ICorDebugReferenceValue";
      return hr;
    }

    hr = debug_reference->IsNull(&local_is_null);

    if (FAILED(hr)) {
      cerr << "Failed to check whether reference is null or not.";
      return hr;
    }

    // Null reference;
    if (local_is_null) {
      break;
    }

    hr = debug_reference->Dereference(&temp_value);
    if (FAILED(hr)) {
      return hr;
    }

    reference_depth++;
  }

  if (reference_depth == kReferenceDepth) {
    cerr << "Cannot dereference more than " << kReferenceDepth << " times!";
    return E_FAIL;
  }

  *is_null = local_is_null;
  (*dereferenced_value) = temp_value;
  temp_value->AddRef();
  return S_OK;
}

HRESULT Unbox(ICorDebugValue *debug_value, ICorDebugValue **unboxed_value) {
  if (!unboxed_value) {
    cerr << "dereferenced_value cannot be a null pointer.";
    return E_INVALIDARG;
  }

  HRESULT hr;
  CComPtr<ICorDebugBoxValue> boxed_value;

  // If it's not a boxed value, don't do anything.
  hr = debug_value->QueryInterface(__uuidof(ICorDebugBoxValue),
                                   reinterpret_cast<void **>(&boxed_value));
  if (hr == E_NOINTERFACE) {
    (*unboxed_value) = debug_value;
    debug_value->AddRef();
    return S_OK;
  } else if (FAILED(hr)) {
    cerr << "Failed to query ICorDebugBoxValue.";
    return hr;
  }

  // Unboxing!
  CComPtr<ICorDebugObjectValue> debug_object_value;
  hr = boxed_value->GetObject(&debug_object_value);
  if (FAILED(hr)) {
    cerr << "Failed get underlying object from boxed object.";
    return hr;
  }

  (*unboxed_value) = debug_object_value;
  (*unboxed_value)->AddRef();

  return S_OK;
}

HRESULT DereferenceAndUnbox(ICorDebugValue *debug_value,
                            ICorDebugValue **dereferenced_and_unboxed_value,
                            BOOL *isNull) {
  HRESULT hr;
  CComPtr<ICorDebugValue> dereferenced_value;
  CComPtr<ICorDebugValue> unboxed_value;

  hr = Dereference(debug_value, &dereferenced_value, isNull);
  if (FAILED(hr)) {
    cerr << "Failed to dereference value.";
    return hr;
  }

  hr = Unbox(dereferenced_value, &unboxed_value);
  if (FAILED(hr)) {
    cerr << "Failed to unbox value.";
    return hr;
  }

  (*dereferenced_and_unboxed_value) = unboxed_value;
  unboxed_value->AddRef();
  return S_OK;
}

HRESULT CreateStrongHandle(ICorDebugValue *debug_value,
                           ICorDebugHandleValue **handle) {
  if (!debug_value) {
    cerr << "debug_value should not be null.";
    return E_INVALIDARG;
  }

  HRESULT hr;
  CComPtr<ICorDebugHeapValue2> heap_value;

  hr = debug_value->QueryInterface(__uuidof(ICorDebugHeapValue2),
                                   reinterpret_cast<void **>(&heap_value));
  if (FAILED(hr)) {
    cerr << "Failed to get heap value from ICorDebugValue.";
    return hr;
  }

  return heap_value->CreateHandle(CorDebugHandleType::HANDLE_STRONG, handle);
}

void PrintWcharString(const WCHAR *wchar_string) {
// PAL_STDCPP_COMPAT is the flag CORECLR uses
// for non-Windows platform. Since we are compiling
// using their headers, we need to use this macro.
#ifdef PAL_STDCPP_COMPAT
  while (wchar_string && *wchar_string) {
    WCHAR current_char = *wchar_string;
    wchar_t truncated = static_cast<wchar_t>(current_char);
    std::wcout << truncated;
    ++wchar_string;
  }
#else
  std::wcout << wchar_string;
#endif
}

void PrintWcharString(const vector<WCHAR> &wchar_vector) {
  PrintWcharString(wchar_vector.data());
}

}  //  namespace google_cloud_debugger
