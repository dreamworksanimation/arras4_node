// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_URL_ROUTER_H__
#define __ARRAS4_URL_ROUTER_H__

#include <vector>
#include <string>
#include <memory>
#include <functional>  // for std::function

// Router allows you to easily map url paths to handler functions. 
// The paths are made of /-separated elements, where each element can be a constant string 
// or a variable.
// These signatures are supported for handler functions:
//    void handle(const HttpServerRequest & req, HttpServerResponse & resp);
//    void handle(const HttpServerRequest & req, HttpServerResponse & resp, 
//                const std::string& var1);
//    void handle(const HttpServerRequest & req, HttpServerResponse & resp, 
//                const std::string& var1, const std::string& var2);
//    void handle(const HttpServerRequest & req, HttpServerResponse & resp, 
//                const std::vector<std::string>& vars);
// For the first three options, if the number of variables in the mapped path doesn't match the signature,
// excess variable values are lost and missing values are set to an empty string. Variables are not named.
// 
// Example:
//   void sessionStatusHandler(const HttpServerRequest & req, 
//                             HttpServerResponse & resp, 
//                             const std::string& sessionId)
//   router.add("/node/1/sessions/*/status",sessionStatusHandler);
namespace arras4 {

    namespace network {
        class HttpServerRequest;
        class HttpServerResponse;
    }

    namespace node {

	class BanList;

        namespace urlrouter {

            class Node;        
            using StrVec = std::vector<std::string>;
            using Fun0 = std::function<void(const network::HttpServerRequest &, 
                                            network::HttpServerResponse &)>;
            using Fun1 = std::function<void(const network::HttpServerRequest &, 
                                            network::HttpServerResponse &,
                                            const std::string&)>;
            using Fun2 = std::function<void(const network::HttpServerRequest &, 
                                            network::HttpServerResponse &,
                                            const std::string&, 
                                            const std::string&)>;
            using FunN = std::function<void(const network::HttpServerRequest &, 
                                            network::HttpServerResponse &,
                                            const StrVec&)>;
        }

        class UrlRouter
        {
        public:
            UrlRouter(urlrouter::Fun0 unmappedHandler);
            ~UrlRouter();

            // add a path, and specify its handler function
            void add(const std::string& path, urlrouter::Fun0 handler);
            void add(const std::string& path, urlrouter::Fun1 handler);
            void add(const std::string& path, urlrouter::Fun2 handler);
            void add(const std::string& path, urlrouter::FunN handler);

            // call the mapped handler, if there is one
            void handle(const network::HttpServerRequest &, 
                        network::HttpServerResponse &);

	    void setBanList(BanList* bl) { mBanList = bl; }
 
        private:
	    bool checkBanList(const network::HttpServerRequest &);

            urlrouter::Node* mRoot;
            urlrouter::Fun0 mUnmappedHandler;
	    BanList* mBanList{nullptr};
        };
    }
}
#endif

