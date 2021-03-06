#ifndef PIPELINE_REDISPIPELINEBOOTSTRAP_H_
#define PIPELINE_REDISPIPELINEBOOTSTRAP_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gflags/gflags.h"
#include "infra/kafka/AbstractConsumer.h"
#include "infra/kafka/ConsumerHelper.h"
#include "infra/kafka/Producer.h"
#include "infra/ScheduledTaskProcessor.h"
#include "infra/ScheduledTaskQueue.h"
#include "librdkafka/rdkafkacpp.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "pipeline/DatabaseManager.h"
#include "pipeline/EmbeddedHttpServer.h"
#include "pipeline/KafkaConsumerConfig.h"
#include "pipeline/RedisHandler.h"
#include "pipeline/RedisHandlerBuilder.h"
#include "pipeline/RedisPipelineFactory.h"
#include "prometheus/exposer.h"
#include "prometheus/registry.h"
#include "wangle/bootstrap/ServerBootstrap.h"

namespace pipeline {

// RedisPipelineBootstrap is a template for launching RedisPipeline-based services in a main function
class RedisPipelineBootstrap {
 public:
  // A factory that create a RedisHandler with optional components from RedisPipelineBootstrap
  using RedisHandlerFactory = std::shared_ptr<RedisHandler> (*)(RedisPipelineBootstrap*);

  // A factory that creates a kafka consumer
  using KafkaConsumerFactory = std::shared_ptr<infra::kafka::AbstractConsumer> (*)(const std::string&,
                                                                                   const KafkaConsumerConfig&,
                                                                                   const std::string&,
                                                                                   RedisPipelineBootstrap*);
  // Map kafka consumer config keys to consumer factories
  using KafkaConsumerFactoryMap = std::unordered_map<std::string, KafkaConsumerFactory>;

  // A factory that creates a database manager with a provided RocksDB column families and db instance
  using DatabaseManagerFactory = std::shared_ptr<DatabaseManager> (*)(const DatabaseManager::ColumnFamilyMap&,
                                                                      bool masterReplica, rocksdb::DB*,
                                                                      RedisPipelineBootstrap*);

  // A factory that creates a ScheduledTaskProcessor instance with a provided database manager
  using ScheduledTaskProcessorFactory =
      std::shared_ptr<infra::ScheduledTaskProcessor> (*)(RedisPipelineBootstrap*);
  // Map column names to ScheduledTaskProcessor factories that use the column names
  using ScheduledTaskProcessorFactoryMap = std::unordered_map<std::string, ScheduledTaskProcessorFactory>;

  // Function to configure a column family in RocksDB, given a defaultBlockCacheSizeMb
  using RocksDbCfConfigurator = void (*)(int, rocksdb::ColumnFamilyOptions*);
  // Map column family names to RocksDbCfConfigurators
  using RocksDbCfConfiguratorMap = std::unordered_map<std::string, RocksDbCfConfigurator>;

  // Function to configure DB-level options for RocksDB
  using RocksDbConfigurator = void (*)(rocksdb::DBOptions*);

  // A RedisHandlerBuilder that creates handler instances using the given factory method
  class DefaultRedisHandlerBuilder : public RedisHandlerBuilder {
   public:
    DefaultRedisHandlerBuilder(RedisHandlerFactory redisHandlerFactory, bool singletonHandler,
                               RedisPipelineBootstrap* bootstrap)
        : redisHandlerFactory_(redisHandlerFactory),
          singletonHandler_(singletonHandler),
          bootstrap_(bootstrap) {
      if (singletonHandler) {
        // No race condition here since the constructor is only called in a single thread running bootstrap
        handler_ = redisHandlerFactory(bootstrap);
        CHECK_NOTNULL(handler_.get());
      }
    }

    std::shared_ptr<RedisHandler> newHandler() override {
      std::shared_ptr<RedisHandler> handler = singletonHandler_ ? handler_ : redisHandlerFactory_(bootstrap_);
      handler_->connectionOpened();
      return handler;
    }

   private:
    RedisHandlerFactory redisHandlerFactory_;
    bool singletonHandler_;
    RedisPipelineBootstrap* bootstrap_;
    std::shared_ptr<RedisHandler> handler_;
  };

  // Defines function pointers to configure a RedisPipeline with optional components
  struct Config {
    // Required
    RedisHandlerFactory redisHandlerFactory = nullptr;

    // Optional
    KafkaConsumerFactoryMap kafkaConsumerFactoryMap;

    // Optional
    DatabaseManagerFactory databaseManagerFactory = nullptr;

    // Optional
    ScheduledTaskProcessorFactoryMap scheduledTaskProcessorFactoryMap;

    // Optional
    // The default column family and smyte metadata column family are created and optimized for point lookups, but
    // their behaviors can be customized by providing corresponding RocksDbCfConfigurators. Additional column families
    // can be created based on the specification of this map. Note that it is not recommended to change the
    // configuration for smyte metadata.
    RocksDbCfConfiguratorMap rocksDbCfConfiguratorMap;

    // Optional
    // Allow client code to set DB-level options for RocksDB
    RocksDbConfigurator rocksDbConfigurator = nullptr;

    // Optional
    // Indicate whether a singleton RedisHandler instance is sufficient for the pipeline
    // It is an optimization for the pipelines that do not save states to the handler instance
    // Most handlers should leave this optional true unless transaction support is need. See counters
    bool singletonRedisHandler = true;

    Config(RedisHandlerFactory _redisHandlerFactory,
           KafkaConsumerFactoryMap _kafkaConsumerFactoryMap = KafkaConsumerFactoryMap(),
           DatabaseManagerFactory _databaseManagerFactory = nullptr,
           ScheduledTaskProcessorFactoryMap _scheduledTaskProcessorFactoryMap = ScheduledTaskProcessorFactoryMap(),
           RocksDbCfConfiguratorMap _rocksDbCfConfiguratorMap = RocksDbCfConfiguratorMap(),
           RocksDbConfigurator _rocksDbConfigurator = nullptr,
           bool _singletonRedisHandler = true)
        : redisHandlerFactory(_redisHandlerFactory),
          kafkaConsumerFactoryMap(_kafkaConsumerFactoryMap),
          databaseManagerFactory(_databaseManagerFactory),
          scheduledTaskProcessorFactoryMap(_scheduledTaskProcessorFactoryMap),
          rocksDbCfConfiguratorMap(std::move(_rocksDbCfConfiguratorMap)),
          rocksDbConfigurator(_rocksDbConfigurator),
          singletonRedisHandler(_singletonRedisHandler) {}
  };

  static std::string getColumnFamilyNameInGroup(const std::string& groupName, int index) {
    return folly::sformat("{}-{}", groupName, index);
  }

  // Called by clients to create an instance to configure and start a server
  static std::shared_ptr<RedisPipelineBootstrap> create(Config config);

  // Create a kafka producer for the given topic
  static std::shared_ptr<infra::kafka::Producer> createKafkaProducer(
      std::string topic, infra::kafka::Producer::Config = infra::kafka::Producer::Config());

  ~RedisPipelineBootstrap() {}

  // Getter methods for optional components.
  std::shared_ptr<pipeline::DatabaseManager> getDatabaseManager() const {
    CHECK_NOTNULL(databaseManager_.get());
    return databaseManager_;
  }
  const DatabaseManager::ColumnFamilyGroupMap& getColumnFamilyGroupMap() const {
    return columnFamilyGroupMap_;
  }
  std::shared_ptr<infra::kafka::ConsumerHelper> getKafkaConsumerHelper() const {
    CHECK_NOTNULL(kafkaConsumerHelper_.get());
    return kafkaConsumerHelper_;
  }
  std::shared_ptr<infra::ScheduledTaskQueue> getScheduledTaskQueue(const std::string& name) const {
    auto it = scheduledTaskQueueMap_.find(name);
    CHECK(it != scheduledTaskQueueMap_.end());
    return it->second;
  }
  std::shared_ptr<infra::kafka::Producer> getKafkaProducer(const std::string& name) const {
    auto it = kafkaProducers_.find(name);
    return it == kafkaProducers_.end() ? std::shared_ptr<infra::kafka::Producer>() : it->second;
  }
  std::shared_ptr<prometheus::Registry> getMetricsRegistry() const {
    CHECK_NOTNULL(metricsRegistry_.get());
    return metricsRegistry_;
  }

  void initializeRocksDb(const std::string& dbPath, const std::string& dbPaths,
                         const std::string& cfGroupConfigs,
                         const std::string& dropCfGroupConfigs, int parallelism, int blockCacheSizeMb,
                         bool createIfMissing, bool createIfMissingOneOff, int64_t versionMimestampMs);

  void stopRocksDb() {
    for (auto& entry : columnFamilyMap_) {
      rocksDb_->DestroyColumnFamilyHandle(entry.second);
    }
    delete rocksDb_;
    LOG(INFO) << "RocksDB has shutdown gracefully";
  }

  // optimize block-based table after all options are initialized
  void optimizeBlockedBasedTable();

  // Initialize optional components
  void initializeDatabaseManager(bool masterReplica);
  void initializeKafkaProducers(const std::string& brokerList, const std::string& kafkaProducerConfigs);
  void initializeKafkaConsumer(const std::string& brokerList, const std::string& kafkaConsumerConfigs,
                               int64_t versionTimestampMs);
  void initializeScheduledTaskQueues();
  void initializeRegistry();

  void initializeEmbeddedHttpServer(int httpPort, int redisServerPort);

  void startOptionalComponents() {
    if (databaseManager_) {
      databaseManager_->start();
    }
    for (auto& taskQueueEntry : scheduledTaskQueueMap_) {
      taskQueueEntry.second->start();
    }
    // First initialize all consumers then start their consumer loops
    // Initialization may panic on verification failures. Panic before starting any consumer loops reduces the
    // probability of data corruption since no writes can be committed until consumer loops start (if you don't use
    // kafka consumers then the following loops are no-op anyway).
    for (auto& consumer : kafkaConsumers_) {
      consumer->init(RdKafka::Topic::OFFSET_STORED);
    }
    for (auto& consumer : kafkaConsumers_) {
      consumer->start();
    }
    if (embeddedHttpServer_) {
      embeddedHttpServer_->start();
    }
  }

  void stopOptionalComponents() {
    // stop in the reverse order of start
    if (embeddedHttpServer_) {
      embeddedHttpServer_->destroy();
    }
    for (auto& consumer : kafkaConsumers_) {
      // call stop first as it's non-blocking and consumers will stop in parallel
      consumer->stop();
    }
    for (auto& consumer : kafkaConsumers_) {
      // destroy is blocking and it will wait for each consumer to completely stop sequentially
      consumer->destroy();
    }
    for (auto& taskQueueEntry : scheduledTaskQueueMap_) {
      taskQueueEntry.second->destroy();
    }
    for (auto& producerEntry : kafkaProducers_) {
      if (producerEntry.second) producerEntry.second->destroy();
    }
    if (databaseManager_) {
      databaseManager_->destroy();
    }
  }

  // Create server and block on listening
  void launchServer(int port, int connectionIdleTimeoutMs);

  // Stop server
  void stopServer() {
    if (server_) {
      server_->stop();
      server_->join();
      // Don't delete server since it holds pointers to things that might be needed by other threads.
      // It's not really leaking memory since we are in the shutdown process anyway.
      server_ = nullptr;
    }
  }

  // Persist version timestamp to rocksdb
  void persistVersionTimestamp(int64_t versionTimestampMs);

  // Get the column family for the given name. Since we only call this during startup time, the program would terminate
  // if column family does not exist, in order to fail out loud.
  rocksdb::ColumnFamilyHandle* getColumnFamily(const std::string& name) {
    CHECK_GT(columnFamilyMap_.count(name), 0) << "Column family not found: " << name;
    return columnFamilyMap_[name];
  }

 private:
  struct RocksDbColumnFamilyGroupConfig {
    int startShardIndex;
    int localVirtualShardCount;
    int shardIndexIncrement;

    RocksDbColumnFamilyGroupConfig(int _startShardIndex, int _localVirtualShardCount, int _shardIndexIncrement)
        : startShardIndex(_startShardIndex),
          localVirtualShardCount(_localVirtualShardCount),
          shardIndexIncrement(_shardIndexIncrement) {}
  };

  using RocksDbColumnFamilyGroupConfigMap = std::unordered_map<std::string, RocksDbColumnFamilyGroupConfig>;

  static constexpr int64_t kMaxVersionTimestampAgeMs = 30 * 60 * 1000;  // 30 minutes
  static constexpr char kVersionTimestampKey[] = "VersionTimestamp";

  explicit RedisPipelineBootstrap(Config config) : config_(std::move(config)), rocksDb_(nullptr) {}

  // Validate if we can apply the one off flags
  bool canApplyOneOffFlags(int64_t versionTimestampMs);

  // Update ColumnFamilyOptions with block cache config for RocksDB
  void setRocksDbBlockCache(int blockCacheSizeMb, rocksdb::ColumnFamilyOptions* options);

  // Set db_paths from json string
  void setDbPaths(const std::string& json, rocksdb::Options* options);

  // Parse configurations for rocksdb column family groups
  RocksDbColumnFamilyGroupConfigMap parseRocksDbColumnFamilyGroupConfigs(const std::string& configs);

  // Process column family group by call the given callback with each column family name in the group in order
  void processRocksDbColumnFamilyGroup(const std::string& groupName, const RocksDbColumnFamilyGroupConfig& groupConfig,
                                       std::function<void(const std::string&)> callback);
  // Configurations for the RedisPipeline
  Config config_;

  // rocksdb pointers here are raw pointers since we want to deleted them explicitly for graceful shutdown
  rocksdb::DB* rocksDb_;
  DatabaseManager::ColumnFamilyMap columnFamilyMap_;
  DatabaseManager::ColumnFamilyGroupMap columnFamilyGroupMap_;
  std::unordered_map<std::string, rocksdb::ColumnFamilyOptions> columnFamilyOptionsMap_;

  // optional components
  std::shared_ptr<DatabaseManager> databaseManager_;
  std::unordered_map<std::string, std::shared_ptr<infra::ScheduledTaskQueue>> scheduledTaskQueueMap_;
  std::shared_ptr<infra::kafka::ConsumerHelper> kafkaConsumerHelper_;
  // Store consumers as a vector because the same topic may be used by multiple consumer classes, and the same
  // consumer class may be used by different topics or the same topic with different configurations
  std::vector<std::shared_ptr<infra::kafka::AbstractConsumer>> kafkaConsumers_;
  // Producers are indexed by logical (canonical) topic names because of 1:1 mapping between topic and producer
  std::unordered_map<std::string, std::shared_ptr<infra::kafka::Producer>> kafkaProducers_;
  // Prometheus metrics
  std::shared_ptr<prometheus::Exposer> metricsExposer_;
  std::shared_ptr<prometheus::Registry> metricsRegistry_;
  // Embedded http server for health check and metrics
  std::shared_ptr<EmbeddedHttpServer> embeddedHttpServer_;
  // require component
  // NOTE: use raw pointer here to avoid automatic deletion of the pointer.
  // server_->stop(); is sufficient for releasing resources
  wangle::ServerBootstrap<pipeline::RedisPipeline>* server_;
};

}  // namespace pipeline

#endif  // PIPELINE_REDISPIPELINEBOOTSTRAP_H_
