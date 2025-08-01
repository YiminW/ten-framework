//
// Copyright © 2025 Agora
// This file is part of TEN Framework, an open source project.
// Licensed under the Apache License, Version 2.0, with certain conditions.
// Refer to the "LICENSE" file in the root directory for more information.
//
#include <nlohmann/json.hpp>
#include <string>

#include "gtest/gtest.h"
#include "include_internal/ten_runtime/binding/cpp/ten.h"
#include "ten_utils/lib/thread.h"
#include "tests/common/client/cpp/msgpack_tcp.h"
#include "tests/ten_runtime/smoke/util/binding/cpp/check.h"

namespace {

class test_extension_1 : public ten::extension_t {
 public:
  explicit test_extension_1(const char *name) : ten::extension_t(name) {}

  void on_configure(ten::ten_env_t &ten_env) override {
    bool rc = ten::ten_env_internal_accessor_t::init_manifest_from_json(
        ten_env,
        // clang-format off
                 R"({
                      "type": "extension",
                      "name": "schema_cmd_property_required__test_extension_1",
                      "version": "0.1.0",
                      "api": {
                        "cmd_out": [
                          {
                            "name": "hello_world",
                            "property": {
                              "properties": {
                                "foo": {
                                  "type": "string"
                                },
                                "bar": {
                                  "type": "int8"
                                }
                              },
                              "required": ["foo"]
                            }
                          }
                        ]
                      }
                    })"
        // clang-format on
    );
    ASSERT_EQ(rc, true);

    ten_env.on_configure_done();
  }

  void on_cmd(ten::ten_env_t &ten_env,
              std::unique_ptr<ten::cmd_t> cmd) override {
    if (cmd->get_name() == "hello_world") {
      // The property `foo` is required.
      bool rc = ten_env.send_cmd(std::move(cmd));
      ASSERT_EQ(rc, false);

      rc = cmd->set_property("foo", "abc");
      ASSERT_EQ(rc, true);

      // The property value is out of range of int8.
      rc = cmd->set_property("bar", 1232);
      ASSERT_EQ(rc, true);

      rc = ten_env.send_cmd(std::move(cmd));
      ASSERT_EQ(rc, false);

      // The `cmd` is still valid if `send_cmd` fails.
      rc = cmd->set_property("bar", 123);
      ASSERT_EQ(rc, true);

      rc = ten_env.send_cmd(std::move(cmd));
      ASSERT_EQ(rc, true);

      return;
    }
  }
};

class test_extension_2 : public ten::extension_t {
 public:
  explicit test_extension_2(const char *name) : ten::extension_t(name) {}

  void on_configure(ten::ten_env_t &ten_env) override {
    bool rc = ten::ten_env_internal_accessor_t::init_manifest_from_json(
        ten_env,
        // clang-format off
                 R"({
                      "type": "extension",
                      "name": "schema_cmd_property_required__test_extension_2",
                      "version": "0.1.0",
                      "api": {
                        "cmd_in": [
                          {
                            "name": "hello_world",
                            "property": {
                              "properties": {
                                "foo": {
                                  "type": "string"
                                },
                                "bar": {
                                  "type": "uint8"
                                }
                              },
                              "required": ["foo"]
                            },
                            "result": {
                              "property": {
                                "properties": {
                                  "detail": {
                                    "type": "object",
                                    "properties": {
                                      "a": {
                                        "type": "string"
                                      },
                                      "b": {
                                        "type": "int8"
                                      }
                                    },
                                    "required": ["a"]
                                  },
                                  "extra": {
                                    "type": "uint16"
                                  }
                                },
                                "required": ["detail"]
                              }
                            }
                          }
                        ]
                      }
                    })"
        // clang-format on
    );
    ASSERT_EQ(rc, true);

    ten_env.on_configure_done();
  }

  void on_cmd(ten::ten_env_t &ten_env,
              std::unique_ptr<ten::cmd_t> cmd) override {
    if (cmd->get_name() == "hello_world") {
      auto cmd_result = ten::cmd_result_t::create(TEN_STATUS_CODE_OK, *cmd);

      auto detail = nlohmann::json::object();
      detail["b"] = 1;

      cmd_result->set_property_from_json("detail", detail.dump().c_str());
      bool rc = ten_env.return_result(std::move(cmd_result));

      // The property `a` is required.
      ASSERT_EQ(rc, false);

      detail["a"] = "demo";
      cmd_result->set_property_from_json("detail", detail.dump().c_str());
      rc = ten_env.return_result(std::move(cmd_result));
      ASSERT_EQ(rc, true);
    }
  }
};

class test_app : public ten::app_t {
 public:
  void on_configure(ten::ten_env_t &ten_env) override {
    bool rc = ten_env.init_property_from_json(
        // clang-format off
        R"({
             "ten": {
               "uri": "msgpack://127.0.0.1:8001/",
               "log": {
                 "level": 2
               }
             }
           })",
        // clang-format on
        nullptr);
    ASSERT_EQ(rc, true);

    ten_env.on_configure_done();
  }
};

void *test_app_thread_main(TEN_UNUSED void *args) {
  auto *app = new test_app();
  app->run();
  delete app;

  return nullptr;
}

TEN_CPP_REGISTER_ADDON_AS_EXTENSION(
    schema_cmd_property_required__test_extension_1, test_extension_1);
TEN_CPP_REGISTER_ADDON_AS_EXTENSION(
    schema_cmd_property_required__test_extension_2, test_extension_2);

}  // namespace

TEST(SchemaTest, SendCmdPropertyRequired) {  // NOLINT
  // Start app.
  auto *app_thread =
      ten_thread_create("app thread", test_app_thread_main, nullptr);

  // Create a client and connect to the app.
  auto *client = new ten::msgpack_tcp_client_t("msgpack://127.0.0.1:8001/");

  // Send graph.
  auto start_graph_cmd = ten::cmd_start_graph_t::create();
  start_graph_cmd->set_graph_from_json(R"({
           "nodes": [{
                "type": "extension",
                "name": "test_extension_1",
                "addon": "schema_cmd_property_required__test_extension_1",
                "extension_group": "basic_extension_group",
                "app": "msgpack://127.0.0.1:8001/"
             },{
                "type": "extension",
                "name": "test_extension_2",
                "addon": "schema_cmd_property_required__test_extension_2",
                "extension_group": "basic_extension_group",
                "app": "msgpack://127.0.0.1:8001/"
             }],
             "connections": [{
               "app": "msgpack://127.0.0.1:8001/",
               "extension": "test_extension_1",
               "cmd": [{
                 "name": "hello_world",
                 "dest": [{
                   "app": "msgpack://127.0.0.1:8001/",
                   "extension": "test_extension_2"
                 }]
               }]
             }]
           })");
  auto cmd_result =
      client->send_cmd_and_recv_result(std::move(start_graph_cmd));
  ten_test::check_status_code(cmd_result, TEN_STATUS_CODE_OK);

  // Send a user-defined 'hello world' command.
  auto hello_world_cmd = ten::cmd_t::create("hello_world");
  hello_world_cmd->set_dests(
      {{"msgpack://127.0.0.1:8001/", "", "test_extension_1"}});
  cmd_result = client->send_cmd_and_recv_result(std::move(hello_world_cmd));
  ten_test::check_status_code(cmd_result, TEN_STATUS_CODE_OK);
  ten_test::check_detail_with_json(cmd_result, R"({"a": "demo", "b": 1})");

  delete client;

  ten_thread_join(app_thread, -1);
}
