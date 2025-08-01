//
// Copyright © 2025 Agora
// This file is part of TEN Framework, an open source project.
// Licensed under the Apache License, Version 2.0, with certain conditions.
// Refer to the "LICENSE" file in the root directory for more information.
//
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "gtest/gtest.h"
#include "include_internal/ten_runtime/binding/cpp/ten.h"
#include "ten_runtime/common/error_code.h"
#include "ten_utils/lang/cpp/lib/error.h"
#include "ten_utils/lib/thread.h"
#include "tests/common/client/cpp/msgpack_tcp.h"
#include "tests/ten_runtime/smoke/util/binding/cpp/check.h"

#define TEST_DATA 12344321

namespace {

class test_extension_1 : public ten::extension_t {
 public:
  explicit test_extension_1(const char *name) : ten::extension_t(name) {}

  void on_configure(ten::ten_env_t &ten_env) override {
    // clang-format off

    bool rc = ten::ten_env_internal_accessor_t::init_manifest_from_json(ten_env,
                 R"({
                      "type": "extension",
                      "name": "no_connection_other_thread__extension_1",
                      "version": "0.1.0",
                      "api": {
                        "cmd_out": [
                          {
                            "name": "test",
                            "property": {
                              "properties": {
                                "test_data": {
                                  "type": "int32"
                                }
                              }
                            }
                          }
                        ]
                      }
                    })");
    // clang-format on
    ASSERT_EQ(rc, true);

    ten_env.on_configure_done();
  }

  void on_cmd(ten::ten_env_t &ten_env,
              std::unique_ptr<ten::cmd_t> cmd) override {
    if (cmd->get_name() == "hello_world") {
      hello_world_cmd = std::move(cmd);

      auto *ten_env_proxy = ten::ten_env_proxy_t::create(ten_env);
      other_thread = std::thread([this, ten_env_proxy]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        ten_env_proxy->notify([this](ten::ten_env_t &ten_env) {
          auto new_cmd = ten::cmd_t::create("test");
          new_cmd->set_property("test_data", TEST_DATA);

          ten::error_t err;
          bool rc = ten_env.send_cmd(
              std::move(new_cmd),
              [](ten::ten_env_t &ten_env,
                 std::unique_ptr<ten::cmd_result_t> cmd_result,
                 ten::error_t *err) { TEN_ASSERT(0, "Should not happen."); },
              &err);
          TEN_ASSERT(!rc, "Should send_cmd failed.");
          TEN_ASSERT(err.error_code() == TEN_ERROR_CODE_MSG_NOT_CONNECTED,
                     "The msg should not connected.");

          auto cmd_result_for_hello_world =
              ten::cmd_result_t::create(TEN_STATUS_CODE_OK, *hello_world_cmd);
          cmd_result_for_hello_world->set_property(
              "detail", hello_world_cmd->get_property_string("detail").c_str());
          ten_env.return_result(std::move(cmd_result_for_hello_world));
        });

        delete ten_env_proxy;
      });
    }
  }

  void on_stop(ten::ten_env_t &ten_env) override {
    if (other_thread.joinable()) {
      other_thread.join();
    }

    ten_env.on_stop_done();
  }

 private:
  std::thread other_thread;
  std::unique_ptr<ten::cmd_t> hello_world_cmd;
};

class test_extension_2 : public ten::extension_t {
 public:
  explicit test_extension_2(const char *name) : ten::extension_t(name) {}

  void on_cmd(ten::ten_env_t &ten_env,
              std::unique_ptr<ten::cmd_t> cmd) override {
    if (cmd->get_name() == "test") {
      auto const test_data = cmd->get_property_int32("test_data");
      TEN_ASSERT(test_data == TEST_DATA, "Invalid argument.");

      auto cmd_result = ten::cmd_result_t::create(TEN_STATUS_CODE_OK, *cmd);
      cmd_result->set_property("detail", "hello world, too");
      ten_env.return_result(std::move(cmd_result));
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

void *test_app_thread_main(TEN_UNUSED void *arg) {
  auto *app = new test_app();
  app->run();
  delete app;

  return nullptr;
}

TEN_CPP_REGISTER_ADDON_AS_EXTENSION(no_connection_other_thread__extension_1,
                                    test_extension_1);
TEN_CPP_REGISTER_ADDON_AS_EXTENSION(no_connection_other_thread__extension_2,
                                    test_extension_2);

}  // namespace

TEST(NoConnectionTest, NoConnectionOtherThread) {  // NOLINT
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
      "name": "no_connection_other_thread__extension_1",
      "addon": "no_connection_other_thread__extension_1",
      "app": "msgpack://127.0.0.1:8001/",
      "extension_group": "no_connection_other_thread__extension_group_1"
    },{
      "type": "extension",
      "name": "no_connection_other_thread__extension_2",
      "addon": "no_connection_other_thread__extension_2",
      "app": "msgpack://127.0.0.1:8001/",
      "extension_group": "no_connection_other_thread__extension_group_2"
    }]
  })");
  auto cmd_result =
      client->send_cmd_and_recv_result(std::move(start_graph_cmd));
  ten_test::check_status_code(cmd_result, TEN_STATUS_CODE_OK);

  // Send a user-defined 'hello world' command.
  auto hello_world_cmd = ten::cmd_t::create("hello_world");
  hello_world_cmd->set_dests({{"msgpack://127.0.0.1:8001/", "",
                               "no_connection_other_thread__extension_1"}});
  cmd_result = client->send_cmd_and_recv_result(std::move(hello_world_cmd));
  ten_test::check_status_code(cmd_result, TEN_STATUS_CODE_OK);

  delete client;

  ten_thread_join(app_thread, -1);
}
