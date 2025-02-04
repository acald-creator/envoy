#include "source/extensions/config_subscription/grpc/grpc_subscription_factory.h"

#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/type_to_endpoint.h"
#include "source/common/config/xds_mux/grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_impl.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"

namespace Envoy {
namespace Config {

SubscriptionPtr
GrpcConfigSubscriptionFactory::create(ConfigSubscriptionFactory::SubscriptionData& data) {
  GrpcMuxSharedPtr mux;
  const envoy::config::core::v3::ApiConfigSource& api_config_source =
      data.config_.api_config_source();
  CustomConfigValidatorsPtr custom_config_validators = std::make_unique<CustomConfigValidatorsImpl>(
      data.validation_visitor_, data.server_, api_config_source.config_validators());
  const std::string control_plane_id = Utility::getGrpcControlPlane(api_config_source).value_or("");

  JitteredExponentialBackOffStrategyPtr backoff_strategy =
      Utility::prepareJitteredExponentialBackOffStrategy(
          api_config_source, data.api_.randomGenerator(), SubscriptionFactory::RetryInitialDelayMs,
          SubscriptionFactory::RetryMaxDelayMs);

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
    mux = std::make_shared<Config::XdsMux::GrpcMuxSotw>(
        Utility::factoryForGrpcApiConfigSource(data.cm_.grpcAsyncClientManager(), api_config_source,
                                               data.scope_, true)
            ->createUncachedRawAsyncClient(),
        data.dispatcher_, sotwGrpcMethod(data.type_url_), data.scope_,
        Utility::parseRateLimitSettings(api_config_source), data.local_info_,
        api_config_source.set_node_on_first_message_only(), std::move(custom_config_validators),
        std::move(backoff_strategy), data.xds_config_tracker_, data.xds_resources_delegate_,
        control_plane_id);
  } else {
    mux = std::make_shared<Config::GrpcMuxImpl>(
        data.local_info_,
        Utility::factoryForGrpcApiConfigSource(data.cm_.grpcAsyncClientManager(), api_config_source,
                                               data.scope_, true)
            ->createUncachedRawAsyncClient(),
        data.dispatcher_, sotwGrpcMethod(data.type_url_), data.scope_,
        Utility::parseRateLimitSettings(api_config_source),
        api_config_source.set_node_on_first_message_only(), std::move(custom_config_validators),
        std::move(backoff_strategy), data.xds_config_tracker_, data.xds_resources_delegate_,
        control_plane_id);
  }
  return std::make_unique<GrpcSubscriptionImpl>(
      std::move(mux), data.callbacks_, data.resource_decoder_, data.stats_, data.type_url_,
      data.dispatcher_, Utility::configSourceInitialFetchTimeout(data.config_),
      /*is_aggregated*/ false, data.options_);
}

SubscriptionPtr
DeltaGrpcConfigSubscriptionFactory::create(ConfigSubscriptionFactory::SubscriptionData& data) {
  GrpcMuxSharedPtr mux;
  const envoy::config::core::v3::ApiConfigSource& api_config_source =
      data.config_.api_config_source();
  CustomConfigValidatorsPtr custom_config_validators = std::make_unique<CustomConfigValidatorsImpl>(
      data.validation_visitor_, data.server_, api_config_source.config_validators());

  JitteredExponentialBackOffStrategyPtr backoff_strategy =
      Utility::prepareJitteredExponentialBackOffStrategy(
          api_config_source, data.api_.randomGenerator(), SubscriptionFactory::RetryInitialDelayMs,
          SubscriptionFactory::RetryMaxDelayMs);

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
    mux = std::make_shared<Config::XdsMux::GrpcMuxDelta>(
        Utility::factoryForGrpcApiConfigSource(data.cm_.grpcAsyncClientManager(), api_config_source,
                                               data.scope_, true)
            ->createUncachedRawAsyncClient(),
        data.dispatcher_, deltaGrpcMethod(data.type_url_), data.scope_,
        Utility::parseRateLimitSettings(api_config_source), data.local_info_,
        api_config_source.set_node_on_first_message_only(), std::move(custom_config_validators),
        std::move(backoff_strategy), data.xds_config_tracker_);
  } else {
    mux = std::make_shared<Config::NewGrpcMuxImpl>(
        Config::Utility::factoryForGrpcApiConfigSource(data.cm_.grpcAsyncClientManager(),
                                                       api_config_source, data.scope_, true)
            ->createUncachedRawAsyncClient(),
        data.dispatcher_, deltaGrpcMethod(data.type_url_), data.scope_,
        Utility::parseRateLimitSettings(api_config_source), data.local_info_,
        std::move(custom_config_validators), std::move(backoff_strategy), data.xds_config_tracker_);
  }
  return std::make_unique<GrpcSubscriptionImpl>(
      std::move(mux), data.callbacks_, data.resource_decoder_, data.stats_, data.type_url_,
      data.dispatcher_, Utility::configSourceInitialFetchTimeout(data.config_),
      /*is_aggregated*/ false, data.options_);
}

SubscriptionPtr
AdsConfigSubscriptionFactory::create(ConfigSubscriptionFactory::SubscriptionData& data) {
  return std::make_unique<GrpcSubscriptionImpl>(
      data.cm_.adsMux(), data.callbacks_, data.resource_decoder_, data.stats_, data.type_url_,
      data.dispatcher_, Utility::configSourceInitialFetchTimeout(data.config_), true,
      data.options_);
}

REGISTER_FACTORY(GrpcConfigSubscriptionFactory, ConfigSubscriptionFactory);
REGISTER_FACTORY(DeltaGrpcConfigSubscriptionFactory, ConfigSubscriptionFactory);
REGISTER_FACTORY(AdsConfigSubscriptionFactory, ConfigSubscriptionFactory);

} // namespace Config
} // namespace Envoy
