#pragma once

#include "fts_sql_template.hpp"

#include "duckdb/parser/qualified_name.hpp"

namespace duckdb {

string GetFTSSchemaName(const QualifiedName &qname);
string GetFTSSchema(const QualifiedName &qname);
SQLTemplateArgument GetFTSSchemaArgument(const QualifiedName &qname);
string GetQualifiedTableName(const QualifiedName &qname);
SQLTemplateArgument GetQualifiedTableArgument(const QualifiedName &qname);

vector<string> GetFTSInsertTriggerNames(const QualifiedName &qname);
vector<string> GetFTSClusteredInsertTriggerNames(const QualifiedName &qname);
vector<string> GetFTSDeleteTriggerNames(const QualifiedName &qname);
vector<string> GetFTSClusteredDeleteTriggerNames(const QualifiedName &qname);
vector<string> GetFTSLayeredInsertTriggerNames(const QualifiedName &qname);
vector<string> GetFTSLayeredDeleteTriggerNames(const QualifiedName &qname);
vector<string> GetFTSTriggerNames(const QualifiedName &qname);

string GetFTSBuildTermsTable(const QualifiedName &qname);
string GetFTSBuildDictTable(const QualifiedName &qname);
string GetFTSTermsStorageTable();
string GetFTSBuildRawDictTable(const QualifiedName &qname);
string GetFTSTermStatsTermIndex(const QualifiedName &qname);
string GetFTSTermGramsGramIndex(const QualifiedName &qname);
string GetFTSTermPrefixesPrefixIndex(const QualifiedName &qname);

string FieldLengthAggregateList(idx_t field_count);
string ZeroFieldLengthList(idx_t field_count, const string &type);
string StatsFieldAggregateList(idx_t field_count, const string &aggregate,
                               const string &fallback, const string &type);

} // namespace duckdb
