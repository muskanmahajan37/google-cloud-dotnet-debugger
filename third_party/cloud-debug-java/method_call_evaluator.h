/**
 * Copyright 2015 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef METHOD_CALL_EVALUATOR_H_
#define METHOD_CALL_EVALUATOR_H_

#include <vector>

#include "expression_evaluator.h"
#include "method_info.h"

namespace google_cloud_debugger {

class ICorDebugHelper;

// Invokes methods specified in expressions.
class MethodCallEvaluator : public ExpressionEvaluator {
 public:
  MethodCallEvaluator(
      std::string method_name,
      std::unique_ptr<ExpressionEvaluator> instance_source,
      std::string possible_class_name,
      std::shared_ptr<ICorDebugHelper> debug_helper,
      std::vector<std::unique_ptr<ExpressionEvaluator>> arguments);

  // Compiles the expression.
  // If there are no instance_source_ and possible_class_name_,
  // then treat this expression as a method with name method_name_
  // in the class the current stack frame is in.
  // If instance_source_ is null, use possible_class_name_ as
  // a fully qualified class and search for method with name method_name_
  // in this class.
  // If instance_source_ is not null, search for method with name
  // method_name_ in this class.
  HRESULT Compile(IDbgStackFrame *stack_frame,
                  ICorDebugILFrame *debug_frame,
                  std::ostream *err_stream) override;

  const TypeSignature &GetStaticType() const override {
    return method_info_.returned_type;
  }

  HRESULT Evaluate(std::shared_ptr<DbgObject> *dbg_object,
                   IEvalCoordinator *eval_coordinator,
                   IDbgObjectFactory *obj_factory,
                   std::ostream *err_stream) const override;

 private:
  // Helper method to evaluate arguments of the method call
  // and return the result in arg_debug_values.
  HRESULT EvaluateArgumentsHelper(
      std::vector<ICorDebugValue *> *arg_debug_values,
      ICorDebugEval *debug_eval,
      IEvalCoordinator *eval_coordinator,
      IDbgObjectFactory *obj_factory,
      std::ostream *err_stream) const;

  // Gets method method_info from class with TypeSignature class_signature.
  // This will set ICorDebugFunction result_method if such a method
  // is found.
  HRESULT GetDebugFunctionFromClassNameHelper(const TypeSignature &class_signature,
                                              IDbgStackFrame *stack_frame,
                                              MethodInfo *method_info,
                                              ICorDebugFunction **result_method);

  // Gets the ICorDebugValue that represents the invoking object of
  // this method call.
  HRESULT GetInvokingObject(ICorDebugValue **invoking_object,
                            IEvalCoordinator *eval_coordinator,
                            IDbgObjectFactory *obj_factory,
                            std::ostream *err_stream) const;

  // Method name (whether it's an instance method or a static method).
  const std::string method_name_;

  // "This" object.
  std::shared_ptr<DbgObject> this_obj_;

  // Source object on which the instance method is invoked. Ignored if the
  // call turns out to be to a static method.
  std::unique_ptr<ExpressionEvaluator> instance_source_;

  // This only applies for non-static method.
  // True if the instance_source_ is the invoking object.
  // Otherwise, "this" will be the invoking object.
  bool instance_source_is_invoking_obj_ = false;

  // Fully qualified class name to try to interpret "method_name_" as a static
  // method.
  const std::string possible_class_name_;

  // Arguments to the method call.
  std::vector<std::unique_ptr<ExpressionEvaluator>> arguments_;

  // The ICorDebugFunction that represents the method being called.
  CComPtr<ICorDebugFunction> matched_method_;

  // Generic type parameters for the class that the method is in.
  // TODO(quoct): Add support for generic method.
  std::vector<CComPtr<ICorDebugType>> current_class_generic_types_;

  // The MethodInfo that represents the method being invoked.
  MethodInfo method_info_;

  // Return value of the method.
  TypeSignature return_type_;

  // Helper methods for dealing with ICorDebug.
  std::shared_ptr<ICorDebugHelper> debug_helper_;

  DISALLOW_COPY_AND_ASSIGN(MethodCallEvaluator);
};

}  // namespace google_cloud_debugger

#endif  // METHOD_CALL_EVALUATOR_H_
