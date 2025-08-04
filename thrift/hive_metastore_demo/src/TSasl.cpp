// This file will be removed when the code is accepted into the Thrift library.
/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License") { you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************/

#include <TSasl.h>

#include <boost/algorithm/string.hpp>
#include <cstring>
#include <sstream>

using boost::algorithm::is_any_of;
using boost::algorithm::join;
using boost::algorithm::split;
using boost::algorithm::to_lower;

namespace sasl {

TSasl::TSasl(const std::string& service, const std::string& serverFQDN, sasl_callback_t* callbacks)
        : service(service), serverFQDN(serverFQDN), authComplete(false), callbacks(callbacks), conn(nullptr) {}

uint8_t* TSasl::unwrap(const uint8_t* incoming, const int offset, const uint32_t len, uint32_t* outLen) {
    uint32_t outputlen;
    uint8_t* output;
    int result;

    result = sasl_decode(conn, reinterpret_cast<const char*>(incoming), len,
                         const_cast<const char**>(reinterpret_cast<char**>(&output)), &outputlen);
    if (result != SASL_OK) {
        throw SaslException(sasl_errdetail(conn));
    }
    *outLen = outputlen;
    return output;
}

uint8_t* TSasl::wrap(const uint8_t* outgoing, const int offset, const uint32_t len, uint32_t* outLen) {
    uint32_t outputlen;
    uint8_t* output;
    int result;

    result = sasl_encode(conn, reinterpret_cast<const char*>(outgoing) + offset, len,
                         const_cast<const char**>(reinterpret_cast<char**>(&output)), &outputlen);
    if (result != SASL_OK) {
        throw SaslException(sasl_errdetail(conn));
    }
    *outLen = outputlen;
    return output;
}

std::string TSasl::getUsername() {
    const char* username;
    int result = sasl_getprop(conn, SASL_USERNAME, reinterpret_cast<const void**>(&username));
    if (result != SASL_OK) {
        std::stringstream ss;
        ss << "Error getting SASL_USERNAME property: " << sasl_errstring(result, nullptr, nullptr);
        throw SaslException(ss.str().c_str());
    }
    // Copy the username and return it to the caller. There is no cleanup/delete call for
    // calls to sasl_getprops, the sasl layer handles the cleanup internally.
    std::string ret(username);

    return ret;
}

TSaslClient::TSaslClient(const std::string& mechanisms, const std::string& authenticationId, const std::string& service,
                         const std::string& serverFQDN, const std::map<std::string, std::string>& props,
                         sasl_callback_t* callbacks)
        : TSasl(service, serverFQDN, callbacks), clientStarted(false), mechList(mechanisms) {
    if (!props.empty()) {
        throw SaslServerImplException("Properties not yet supported");
    }
    /*
  if (!authenticationId.empty()) {
    // TODO: setup security property
    sasl_security_properties_t secprops;
    // populate  secprops
    result = sasl_setprop(conn, SASL_AUTH_EXTERNAL, authenticationId.c_str());
  }
  */
}

void TSaslClient::setupSaslContext() {
    int result = sasl_client_new(service.c_str(), serverFQDN.c_str(), nullptr, nullptr, callbacks, 0, &conn);
    if (result != SASL_OK) {
        if (conn != nullptr) {
            throw SaslServerImplException(sasl_errdetail(conn));
        } else {
            throw SaslServerImplException(sasl_errstring(result, nullptr, nullptr));
        }
    }
}

void TSaslClient::resetSaslContext() {
    clientStarted = false;
    authComplete = false;
    disposeSaslContext();
}

/* Evaluates the challenge data and generates a response. */
uint8_t* TSaslClient::evaluateChallengeOrResponse(const uint8_t* challenge, const uint32_t len, uint32_t* resLen) {
    sasl_interact_t* client_interact = nullptr;
    uint8_t* out = nullptr;
    uint32_t outlen = 0;
    uint32_t result;
    char* mechUsing;

    if (!clientStarted) {
        result = sasl_client_start(conn, mechList.c_str(), &client_interact, /* filled in if an interaction is needed */
                                   const_cast<const char**>(reinterpret_cast<char**>(&out)), /* filled in on success */
                                   &outlen,                                                  /* filled in on success */
                                   const_cast<const char**>(&mechUsing));
        clientStarted = true;
        if (result == SASL_OK || result == SASL_CONTINUE) {
            chosenMech = mechUsing;
        }
    } else {
        if (len > 0) {
            result = sasl_client_step(
                    conn,                                                     /* our context */
                    reinterpret_cast<const char*>(challenge),                 /* the data from the server */
                    len,                                                      /* its length */
                    &client_interact,                                         /* this should be unallocated and NULL */
                    const_cast<const char**>(reinterpret_cast<char**>(&out)), /* filled in on success */
                    &outlen);                                                 /* filled in on success */
        } else {
            result = SASL_CONTINUE;
        }
    }

    if (result == SASL_OK) {
        authComplete = true;
    } else if (result != SASL_CONTINUE) {
        throw SaslClientImplException(sasl_errdetail(conn));
    }
    *resLen = outlen;
    return out;
}

/* Returns the IANA-registered mechanism name of this SASL client. */
std::string TSaslClient::getMechanismName() {
    return chosenMech;
}

/* Retrieves the negotiated property */
std::string TSaslClient::getNegotiatedProperty(const std::string& propName) {
    return nullptr;
}

/* Determines whether this mechanism has an optional initial response. */
bool TSaslClient::hasInitialResponse() {
    return true;
}

TSaslServer::TSaslServer(const std::string& service, const std::string& serverFQDN, const std::string& userRealm,
                         unsigned flags, sasl_callback_t* callbacks)
        : TSasl(service, serverFQDN, callbacks), userRealm(userRealm), flags(flags), serverStarted(false) {}

void TSaslServer::setupSaslContext() {
    int result = sasl_server_new(service.c_str(), serverFQDN.size() == 0 ? nullptr : serverFQDN.c_str(),
                                 userRealm.size() == 0 ? nullptr : userRealm.c_str(), nullptr, nullptr, callbacks,
                                 flags, &conn);
    if (result != SASL_OK) {
        if (conn != nullptr) {
            throw SaslServerImplException(sasl_errdetail(conn));
        } else {
            throw SaslServerImplException(sasl_errstring(result, nullptr, nullptr));
        }
    }
}

void TSaslServer::resetSaslContext() {
    serverStarted = false;
    authComplete = false;
    disposeSaslContext();
}

uint8_t* TSaslServer::evaluateChallengeOrResponse(const uint8_t* response, const uint32_t len, uint32_t* resLen) {
    uint8_t* out = nullptr;
    uint32_t outlen = 0;
    uint32_t result;

    if (!serverStarted) {
        result = sasl_server_start(conn, reinterpret_cast<const char*>(response), nullptr, 0,
                                   const_cast<const char**>(reinterpret_cast<char**>(&out)), &outlen);
    } else {
        result = sasl_server_step(conn, reinterpret_cast<const char*>(response), len,
                                  const_cast<const char**>(reinterpret_cast<char**>(&out)), &outlen);
    }

    if (result == SASL_OK) {
        authComplete = true;
    } else if (result != SASL_CONTINUE) {
        throw SaslServerImplException(sasl_errdetail(conn));
    }
    serverStarted = true;

    *resLen = outlen;
    return out;
}

std::string TSaslServer::getMechanismName() {
    const char* mechName;
    int result = sasl_getprop(conn, SASL_MECHNAME, reinterpret_cast<const void**>(&mechName));
    if (result != SASL_OK) {
        std::stringstream ss;
        ss << "Error getting SASL_MECHNAME property: " << sasl_errstring(result, nullptr, nullptr);
        throw SaslException(ss.str().c_str());
    }
    std::string ret(mechName);
    return ret;
}
}; // namespace sasl
