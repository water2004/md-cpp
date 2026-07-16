// elmd.core.instrumentation — testable counters for architectural hot-path gates.
export module elmd.core.instrumentation;
import std;

export namespace elmd {

struct CoreOperationCounters {
    std::uint64_t full_document_parses = 0;
    std::uint64_t full_document_serializations = 0;
    std::uint64_t full_tree_transaction_diffs = 0;
    std::uint64_t full_document_node_id_scans = 0;
    std::uint64_t full_document_symbol_derivations = 0;
    std::uint64_t full_document_outline_derivations = 0;
    std::uint64_t local_symbol_derivations = 0;
    std::uint64_t render_source_key_derivations = 0;
    std::uint64_t full_document_block_index_scans = 0;
    std::uint64_t incremental_document_block_index_repairs = 0;
    std::uint64_t full_document_text_projections = 0;
    std::uint64_t inline_reparses = 0;
};

inline thread_local CoreOperationCounters core_operation_counters;

inline void reset_core_operation_counters() { core_operation_counters = {}; }
inline CoreOperationCounters read_core_operation_counters() { return core_operation_counters; }
inline void record_full_document_parse() { ++core_operation_counters.full_document_parses; }
inline void record_full_document_serialization() { ++core_operation_counters.full_document_serializations; }
inline void record_full_tree_transaction_diff() { ++core_operation_counters.full_tree_transaction_diffs; }
inline void record_full_document_node_id_scan() { ++core_operation_counters.full_document_node_id_scans; }
inline void record_full_document_symbol_derivation() {
    ++core_operation_counters.full_document_symbol_derivations;
}
inline void record_full_document_outline_derivation() {
    ++core_operation_counters.full_document_outline_derivations;
}
inline void record_local_symbol_derivation() { ++core_operation_counters.local_symbol_derivations; }
inline void record_render_source_key_derivation() {
    ++core_operation_counters.render_source_key_derivations;
}
inline void record_full_document_block_index_scan() {
    ++core_operation_counters.full_document_block_index_scans;
}
inline void record_incremental_document_block_index_repair() {
    ++core_operation_counters.incremental_document_block_index_repairs;
}
inline void record_full_document_text_projection() {
    ++core_operation_counters.full_document_text_projections;
}
inline void record_inline_reparse() { ++core_operation_counters.inline_reparses; }

} // namespace elmd
