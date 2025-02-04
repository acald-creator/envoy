#include <memory>

#include "source/common/config/metadata.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/generic_proxy/filters/network/source/match.h"
#include "contrib/generic_proxy/filters/network/source/route.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"
#include "contrib/generic_proxy/filters/network/test/mocks/route.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

class RouteEntryImplTest : public testing::Test {
public:
  class RouteConfig : public RouteSpecificFilterConfig {};

  void initialize(const std::string& yaml_config) {
    ProtoRouteAction proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    route_ = std::make_shared<RouteEntryImpl>(proto_config, server_context_);
  }

protected:
  NiceMock<MockStreamFilterConfig> filter_config_;

  absl::flat_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> route_config_map_;

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  RouteEntryConstSharedPtr route_;
};

/**
 * Test the method that get cluster name from route entry.
 */
TEST_F(RouteEntryImplTest, SimpleClusterName) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->clusterName(), "cluster_0");
};

/**
 * Test the method that get filter metadata from the route entry.
 */
TEST_F(RouteEntryImplTest, RouteMetadata) {
  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    metadata:
      filter_metadata:
        mock_filter:
          key_0: value_0
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(
      "value_0",
      Config::Metadata::metadataValue(&route_->metadata(), "mock_filter", "key_0").string_value());
};

/**
 * Test the method that get route level per filter config from the route entry. This test also
 * verifies that the proto per filter config can be loaded correctly.
 */
TEST_F(RouteEntryImplTest, RoutePerFilterConfig) {
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  ON_CALL(filter_config_, createRouteSpecificFilterConfig(_, _, _))
      .WillByDefault(
          Invoke([this](const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                        ProtobufMessage::ValidationVisitor&) {
            auto route_config = std::make_shared<RouteConfig>();
            route_config_map_.emplace(filter_config_.name(), route_config);
            return route_config;
          }));

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"),
            route_config_map_.at("envoy.filters.generic.mock_filter").get());
};

/**
 * Test the case where there is no route level proto available for the filter.
 */
TEST_F(RouteEntryImplTest, NullRouteEmptyProto) {
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);

  ON_CALL(filter_config_, createRouteSpecificFilterConfig(_, _, _))
      .WillByDefault(
          Invoke([this](const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                        ProtobufMessage::ValidationVisitor&) {
            auto route_config = std::make_shared<RouteConfig>();
            route_config_map_.emplace(filter_config_.name(), route_config);
            return route_config;
          }));

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"), nullptr);
};

/**
 * Test the case where there is no route level config available for the filter.
 */
TEST_F(RouteEntryImplTest, NullRouteSpecificConfig) {
  Registry::InjectFactory<NamedFilterConfigFactory> registration(filter_config_);
  ON_CALL(filter_config_, createEmptyRouteConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    per_filter_config:
      envoy.filters.generic.mock_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value: { "key_0": "value_0" }
  )EOF";
  initialize(yaml_config);

  EXPECT_EQ(route_->perFilterConfig("envoy.filters.generic.mock_filter"), nullptr);
};

/**
 * Test the simple route action wrapper.
 */
TEST(RouteMatchActionTest, SimpleRouteMatchActionTest) {
  auto entry = std::make_shared<NiceMock<MockRouteEntry>>();
  RouteMatchAction action(entry);

  EXPECT_EQ(action.route().get(), entry.get());
}

/**
 * Test the simple data input validator.
 */
TEST(RouteActionValidationVisitorTest, SimpleRouteActionValidationVisitorTest) {
  RouteActionValidationVisitor visitor;
  ServiceMatchDataInputFactory factory;

  EXPECT_EQ(visitor.performDataInputValidation(factory, ""), absl::OkStatus());
}

/**
 * Test the route match action factory.
 */
TEST(RouteMatchActionFactoryTest, SimpleRouteMatchActionFactoryTest) {
  RouteMatchActionFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_EQ("envoy.matching.action.generic_proxy.route", factory.name());

  EXPECT_EQ(factory.createEmptyConfigProto()->GetTypeName(), ProtoRouteAction().GetTypeName());

  const std::string yaml_config = R"EOF(
    cluster: cluster_0
    metadata:
      filter_metadata:
        mock_filter:
          key_0: value_0
  )EOF";
  ProtoRouteAction proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);
  RouteActionContext context{server_context};

  auto factory_cb = factory.createActionFactoryCb(proto_config, context,
                                                  server_context.messageValidationVisitor());

  EXPECT_EQ(factory_cb()->getTyped<RouteMatchAction>().route().get(),
            factory_cb()->getTyped<RouteMatchAction>().route().get());

  EXPECT_EQ(factory_cb()->getTyped<RouteMatchAction>().route()->clusterName(), "cluster_0");
}

class RouteMatcherImplTest : public testing::Test {
public:
  void initialize(const std::string& yaml_config) {
    ProtoRouteConfiguration proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);
    route_matcher_ = std::make_unique<RouteMatcherImpl>(proto_config, factory_context_);
  }

protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  std::unique_ptr<RouteMatcherImpl> route_matcher_;
};

static const std::string RouteConfigurationYaml = R"EOF(
name: test_matcher_tree
virtual_hosts:
- name: service
  hosts:
  - service_0
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "service_0"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_service: match_service
- name: prefix
  hosts:
  - "prefix*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "prefix_service_0"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_1"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_prefix: match_prefix
- name: suffix
  hosts:
  - "*suffix"
  routes:
    matcher_list:
      matchers:
      - predicate:
          and_matcher:
            predicate:
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.host
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
                value_match:
                  exact: "service_0_suffix"
            - single_predicate:
                input:
                  name: envoy.matching.generic_proxy.input.method
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput
                value_match:
                  exact: "method_0"
            - or_matcher:
                predicate:
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_0"
                    value_match:
                      exact: "value_0"
                - single_predicate:
                    input:
                      name: envoy.matching.generic_proxy.input.property
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                        property_name: "key_1"
                    value_match:
                      exact: "value_1"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_2"
              metadata:
                filter_metadata:
                  mock_filter:
                    match_suffix: match_suffix
- name: catch_all
  hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.property
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                property_name: "catch_all"
            value_match:
              exact: "catch_all"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_3"
              metadata:
                filter_metadata:
                  mock_filter:
                    catch_all: catch_all
)EOF";

/**
 * Test the simple name method.
 */
TEST_F(RouteMatcherImplTest, SimpleNameMethod) {
  initialize(RouteConfigurationYaml);
  EXPECT_EQ(route_matcher_->name(), "test_matcher_tree");
}

/**
 * Test the case where the request matches a route entry in the matching tree.
 */
TEST_F(RouteMatcherImplTest, RouteMatch) {
  initialize(RouteConfigurationYaml);

  // Exact host searching.
  {
    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "service_0";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "service_0";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});

    auto route_entry_0 = route_matcher_->routeEntry(fake_request_0);
    auto route_entry_1 = route_matcher_->routeEntry(fake_request_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_0");
  }

  // Prefix host searching.
  {
    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "prefix_service_0";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "prefix_service_0";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});

    auto route_entry_0 = route_matcher_->routeEntry(fake_request_0);
    auto route_entry_1 = route_matcher_->routeEntry(fake_request_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_1");
  }

  // Suffix host searching.
  {
    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "service_0_suffix";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"key_0", "value_0"});

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "service_0_suffix";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"key_1", "value_1"});

    auto route_entry_0 = route_matcher_->routeEntry(fake_request_0);
    auto route_entry_1 = route_matcher_->routeEntry(fake_request_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_2");
  }

  // Catch all host.
  {
    FakeStreamCodecFactory::FakeRequest fake_request_0;
    fake_request_0.host_ = "any_service";
    fake_request_0.method_ = "method_0";
    fake_request_0.data_.insert({"catch_all", "catch_all"});

    FakeStreamCodecFactory::FakeRequest fake_request_1;
    fake_request_1.host_ = "any_service";
    fake_request_1.method_ = "method_0";
    fake_request_1.data_.insert({"catch_all", "catch_all"});

    auto route_entry_0 = route_matcher_->routeEntry(fake_request_0);
    auto route_entry_1 = route_matcher_->routeEntry(fake_request_1);

    EXPECT_EQ(route_entry_0.get(), route_entry_1.get());
    EXPECT_NE(route_entry_0.get(), nullptr);

    EXPECT_EQ(route_entry_0->clusterName(), "cluster_3");
  }
}

/**
 * Test the case where the request not matches any route entry in the matching tree.
 */
TEST_F(RouteMatcherImplTest, RouteNotMatch) {
  initialize(RouteConfigurationYaml);

  // Test the service not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "prefix_service_1";
    fake_request.method_ = "method_0";
    fake_request.data_.insert({"key_0", "value_0"});

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }

  // Test the method not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_x";
    fake_request.data_.insert({"key_0", "value_0"});

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }

  // Test the headers not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "service_0";
    fake_request.method_ = "method_0";
    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }
}

static const std::string RouteConfigurationYamlWithUnknownInput = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.unknown_input
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.UnknownInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationWithUnknownInput) {
  EXPECT_THROW(initialize(RouteConfigurationYamlWithUnknownInput), EnvoyException);
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithoutDefaultHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "service_0"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, NoHostMatch) {
  initialize(RouteConfigurationYamlWithoutDefaultHost);

  // Test the host not match.
  {
    FakeStreamCodecFactory::FakeRequest fake_request;
    fake_request.host_ = "any_service";
    fake_request.method_ = "method_0";
    fake_request.data_.insert({"key_0", "value_0"});

    EXPECT_EQ(nullptr, route_matcher_->routeEntry(fake_request));
  }
}

static const std::string RouteConfigurationYamlWithRepeatedHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "service_0"
  - "service_0"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithRepeatedHost) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithRepeatedHost), EnvoyException,
                            "Only unique values for host are permitted. Duplicate "
                            "entry of domain service_0 in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithMultipleWildcard = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithMultipleWildcard) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize(RouteConfigurationYamlWithMultipleWildcard), EnvoyException,
      "Only a single wildcard domain is permitted in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithMultipleWildcard2 = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - "*"
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
routes:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: envoy.matching.generic_proxy.input.host
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
          value_match:
            exact: "service_0"
      on_match:
        action:
          name: envoy.matching.action.generic_proxy.route
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
            cluster: "cluster_0"
            metadata:
              filter_metadata:
                mock_filter:
                  key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithMultipleWildcard2) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithMultipleWildcard2), EnvoyException,
                            "'routes' cannot be specified at the same time as a "
                            "catch-all ('*') virtual host in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

static const std::string RouteConfigurationYamlWithEmptyHost = R"EOF(
name: test_matcher_tree
virtual_hosts:
- hosts:
  - ""
  routes:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: envoy.matching.generic_proxy.input.host
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput
            value_match:
              exact: "service_0"
        on_match:
          action:
            name: envoy.matching.action.generic_proxy.route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
              cluster: "cluster_0"
              metadata:
                filter_metadata:
                  mock_filter:
                    key_0: value_0
)EOF";

TEST_F(RouteMatcherImplTest, RouteConfigurationYamlWithEmptyHost) {
  EXPECT_THROW_WITH_MESSAGE(initialize(RouteConfigurationYamlWithEmptyHost), EnvoyException,
                            "Invalid empty host name in route test_matcher_tree");
  EXPECT_EQ(nullptr, route_matcher_.get());
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
