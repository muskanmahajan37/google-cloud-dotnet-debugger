﻿// Copyright 2017 Google Inc. All Rights Reserved.
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

using Google.Api.Gax;
using Google.Cloud.Debugger.V2;
using Google.Protobuf;
using Grpc.Core;
using System;
using System.Collections.Generic;
using StackdriverBreakpoint = Google.Cloud.Debugger.V2.Breakpoint;

namespace Google.Cloud.Diagnostics.Debug
{
    /// <summary>
    /// A wrapper around a <see cref="Controller2Client"/> that manages the current agent and
    /// re-registering the agent if it deactivates due to inactivity.
    /// </summary>
    internal class DebuggerClient : IDebuggerClient
    {
        internal const string InitialWaitToken = "init";
        private readonly object _mutex = new object();
        private readonly AgentOptions _options;
        private readonly Controller2Client _controlClient;
        private Debuggee _debuggee;
        private string _waitToken = InitialWaitToken;

        /// <summary>
        /// Create a new <see cref="DebuggerClient"/>
        /// </summary>
        internal DebuggerClient(AgentOptions options, Controller2Client controlClient = null)
        {
            _controlClient = controlClient ?? Controller2Client.Create();
            _options = GaxPreconditions.CheckNotNull(options, nameof(options));
        }

        /// <inheritdoc />
        public void Register()
        {
            lock (_mutex)
            {
                var debuggee = DebuggeeUtils.CreateDebuggee(
                    _options.ProjectId, _options.Module, _options.Version, _options.SourceContext);
                _debuggee = _controlClient.RegisterDebuggee(debuggee).Debuggee;
            
                if (_debuggee.IsDisabled)
                {
                    throw new DebuggeeDisabledException($"'{_debuggee.Id}' is disabled.");
                }
            }
        }

        /// <inheritdoc />
        public IEnumerable<StackdriverBreakpoint> ListBreakpoints()
        {
            if (_debuggee == null)
            {
                throw new InvalidOperationException("Debuggee has not been registered.");
            }

            lock (_mutex)
            {
                var response = TryAction(() =>
                {
                    var request = new ListActiveBreakpointsRequest
                    {
                        DebuggeeId = _debuggee.Id,
                        SuccessOnTimeout = true,
                        WaitToken = _waitToken,
                    };
                    return _controlClient.ListActiveBreakpoints(request);
                });
                _waitToken = response.NextWaitToken ?? InitialWaitToken;
                return response.WaitExpired ? null : response.Breakpoints;
            }
        }

        /// <inheritdoc />
        public IMessage UpdateBreakpoint(StackdriverBreakpoint breakpoint)
        {
            if (_debuggee == null)
            {
                throw new InvalidOperationException("Debuggee has not been registered.");
            }
            GaxPreconditions.CheckNotNull(breakpoint, nameof(breakpoint));
            return TryAction(() => _controlClient.UpdateActiveBreakpoint(_debuggee.Id, breakpoint));
        }

        /// <summary>
        /// Tries a call to the debugger API.  If the debuggee is not found attempt to register it.
        /// </summary>
        /// <exception cref="DebuggeeDisabledException">If the debuggee should be disabled.</exception>
        private T TryAction<T>(Func<T> func)
        {
            try
            {
                return func();
            }
            catch (RpcException e) when (e.Status.StatusCode == StatusCode.NotFound)
            {
                // The debuggee was not found try to register again.
                Register();
                return func();
            }
        }
    }
}
