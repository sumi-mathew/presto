/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "presto_cpp/main/types/PrestoToVeloxSplit.h"
#include <gtest/gtest.h>
#include "presto_cpp/main/connectors/PrestoToVeloxConnector.h"
#include "velox/connectors/hive/HiveConnectorSplit.h"

using namespace facebook::velox;
using namespace facebook::presto;

namespace {
protocol::ScheduledSplit makeHiveScheduledSplit() {
  protocol::ScheduledSplit scheduledSplit;
  scheduledSplit.sequenceId = 111;
  scheduledSplit.planNodeId = "planNodeId-0";

  protocol::Split split;
  split.connectorId = "split.connectorId-0";
  auto hiveTransactionHandle =
      std::make_shared<protocol::hive::HiveTransactionHandle>();
  hiveTransactionHandle->uuid = "split.transactionHandle.uuid-0";
  split.transactionHandle = hiveTransactionHandle;

  auto hiveSplit = std::make_shared<protocol::hive::HiveSplit>();
  hiveSplit->fileSplit.path = "/file/path";
  hiveSplit->storage.storageFormat.inputFormat =
      "com.facebook.hive.orc.OrcInputFormat";
  hiveSplit->fileSplit.start = 0;
  hiveSplit->fileSplit.length = 100;

  split.connectorSplit = hiveSplit;
  scheduledSplit.split = split;
  return scheduledSplit;
}
} // namespace

class PrestoToVeloxSplitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registerPrestoToVeloxConnector(
        std::make_unique<HivePrestoToVeloxConnector>("hive"));
  }

  void TearDown() override {
    unregisterPrestoToVeloxConnector("hive");
  }
};

TEST_F(PrestoToVeloxSplitTest, nullPartitionKey) {
  auto scheduledSplit = makeHiveScheduledSplit();
  auto hiveSplit = std::dynamic_pointer_cast<protocol::hive::HiveSplit>(
      scheduledSplit.split.connectorSplit);
  protocol::hive::HivePartitionKey partitionKey{"nullPartitionKey", nullptr};
  hiveSplit->partitionKeys.push_back(partitionKey);
  auto veloxSplit = toVeloxSplit(scheduledSplit);
  std::shared_ptr<connector::hive::HiveConnectorSplit> veloxHiveSplit;
  ASSERT_NO_THROW({
    veloxHiveSplit =
        std::dynamic_pointer_cast<connector::hive::HiveConnectorSplit>(
            veloxSplit.connectorSplit);
  });
  ASSERT_EQ(
      hiveSplit->partitionKeys.size(), veloxHiveSplit->partitionKeys.size());
  ASSERT_FALSE(
      veloxHiveSplit->partitionKeys.at("nullPartitionKey").has_value());
}

TEST_F(PrestoToVeloxSplitTest, customSplitInfo) {
  auto scheduledSplit = makeHiveScheduledSplit();
  auto& hiveSplit = static_cast<protocol::hive::HiveSplit&>(
      *scheduledSplit.split.connectorSplit);
  hiveSplit.fileSplit.customSplitInfo["foo"] = "bar";
  auto veloxSplit = toVeloxSplit(scheduledSplit);
  auto* veloxHiveSplit =
      dynamic_cast<const connector::hive::HiveConnectorSplit*>(
          veloxSplit.connectorSplit.get());
  ASSERT_TRUE(veloxHiveSplit);
  ASSERT_EQ(veloxHiveSplit->customSplitInfo.size(), 1);
  ASSERT_EQ(veloxHiveSplit->customSplitInfo.at("foo"), "bar");
}

TEST_F(PrestoToVeloxSplitTest, extraFileInfo) {
  auto scheduledSplit = makeHiveScheduledSplit();
  auto& hiveSplit = static_cast<protocol::hive::HiveSplit&>(
      *scheduledSplit.split.connectorSplit);
  hiveSplit.fileSplit.extraFileInfo =
      std::make_shared<std::string>(encoding::Base64::encode("quux"));
  auto veloxSplit = toVeloxSplit(scheduledSplit);
  auto* veloxHiveSplit =
      dynamic_cast<const connector::hive::HiveConnectorSplit*>(
          veloxSplit.connectorSplit.get());
  ASSERT_TRUE(veloxHiveSplit);
  ASSERT_TRUE(veloxHiveSplit->extraFileInfo);
  ASSERT_EQ(*veloxHiveSplit->extraFileInfo, "quux");
}

TEST_F(PrestoToVeloxSplitTest, serdeParameters) {
  auto scheduledSplit = makeHiveScheduledSplit();
  auto& hiveSplit = dynamic_cast<protocol::hive::HiveSplit&>(
      *scheduledSplit.split.connectorSplit);
  hiveSplit.storage.serdeParameters[dwio::common::SerDeOptions::kFieldDelim] =
      "\t";
  hiveSplit.storage
      .serdeParameters[dwio::common::SerDeOptions::kCollectionDelim] = ",";
  hiveSplit.storage.serdeParameters[dwio::common::SerDeOptions::kMapKeyDelim] =
      "|";

  auto veloxSplit = toVeloxSplit(scheduledSplit);
  auto* veloxHiveSplit =
      dynamic_cast<const connector::hive::HiveConnectorSplit*>(
          veloxSplit.connectorSplit.get());
  ASSERT_TRUE(veloxHiveSplit);
  ASSERT_EQ(veloxHiveSplit->serdeParameters.size(), 3);
  ASSERT_EQ(
      veloxHiveSplit->serdeParameters.at(
          dwio::common::SerDeOptions::kFieldDelim),
      "\t");
  ASSERT_EQ(
      veloxHiveSplit->serdeParameters.at(
          dwio::common::SerDeOptions::kCollectionDelim),
      ",");
  ASSERT_EQ(
      veloxHiveSplit->serdeParameters.at(
          dwio::common::SerDeOptions::kMapKeyDelim),
      "|");
}

TEST_F(PrestoToVeloxSplitTest, bucketConversion) {
  auto scheduledSplit = makeHiveScheduledSplit();
  auto& hiveSplit = static_cast<protocol::hive::HiveSplit&>(
      *scheduledSplit.split.connectorSplit);
  hiveSplit.tableBucketNumber = std::make_shared<int>(42);
  hiveSplit.bucketConversion =
      std::make_shared<protocol::hive::BucketConversion>();
  hiveSplit.bucketConversion->tableBucketCount = 4096;
  hiveSplit.bucketConversion->partitionBucketCount = 512;
  auto& column = hiveSplit.bucketConversion->bucketColumnHandles.emplace_back();
  column.name = "c0";
  column.hiveType = "bigint";
  column.typeSignature = "bigint";
  column.columnType = protocol::hive::ColumnType::REGULAR;
  auto veloxSplit = toVeloxSplit(scheduledSplit);
  const auto& veloxHiveSplit =
      static_cast<const connector::hive::HiveConnectorSplit&>(
          *veloxSplit.connectorSplit);
  ASSERT_TRUE(veloxHiveSplit.bucketConversion.has_value());
  ASSERT_EQ(veloxHiveSplit.bucketConversion->tableBucketCount, 4096);
  ASSERT_EQ(veloxHiveSplit.bucketConversion->partitionBucketCount, 512);
  ASSERT_EQ(veloxHiveSplit.bucketConversion->bucketColumnHandles.size(), 1);
  ASSERT_EQ(veloxHiveSplit.infoColumns.at("$path"), hiveSplit.fileSplit.path);
  ASSERT_EQ(veloxHiveSplit.infoColumns.at("$bucket"), "42");
  auto& veloxColumn = veloxHiveSplit.bucketConversion->bucketColumnHandles[0];
  ASSERT_EQ(veloxColumn->name(), "c0");
  ASSERT_EQ(*veloxColumn->dataType(), *BIGINT());
  ASSERT_EQ(*veloxColumn->hiveType(), *BIGINT());
  ASSERT_EQ(
      veloxColumn->columnType(),
      connector::hive::HiveColumnHandle::ColumnType::kRegular);
}
