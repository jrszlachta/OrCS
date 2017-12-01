/// This is the main header file, it contains all the prototypes and
/// describes the relations between classes.
#ifndef _ORCS_ORCS_HPP_
#define _ORCS_ORCS_HPP_

/// C Includes
#include <unistd.h>     /* for getopt */
#include <getopt.h>     /* for getopt_long; POSIX standard getopt is in unistd.h */
#include <inttypes.h>   /* for uint32_t */
#include <zlib.h>

/// C++ Includes
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>


// ============================================================================
/// Classes
// ============================================================================
class orcs_engine_t;
class trace_reader_t;
class opcode_package_t;
class processor_t;

// ============================================================================
/// Global SINUCA_ENGINE instantiation
// ============================================================================
extern orcs_engine_t orcs_engine;

// ===========================================================================
/// Definition for BTB Simulator
// ===========================================================================
#define BTB_PENALTY	8
#define BTB_LINES 	512
#define BTB_WAYS	4

// ===========================================================================
/// Definitions for: GShare/Path Predictor
// ===========================================================================
#define PRED_TABLES  16
#define PRED_WEIGHTS 1024

// ===========================================================================
/// Definition for Cache Simulator
/// L1: 64KB; L2: 1MB; Block: 64B
// ===========================================================================
#define L1_PENALTY	1
#define L1_WAYS		4
#define L1_LINES	1024
#define L1_SET_MASK	(L1_LINES/L1_WAYS - 1)
#define L2_PENALTY	4
#define L2_WAYS		8
#define L2_LINES	16384
#define L2_SET_MASK	(L2_LINES/L2_WAYS - 1)

// ============================================================================
/// Definitions for: Look Ahead Path Prefetcher
// ============================================================================
#define ST_LINES	1024
#define ST_WAYS		2
#define ST_SET_MASK (ST_LINES/ST_WAYS - 1)
#define ST_TAG_BIT	8
#define SIG_LENGTH	12
#define SIG_SHIFT	3
#define PT_LINES	16384
#define PT_WAYS		4
#define PT_SET_MASK (PT_LINES/PT_WAYS - 1)
#define PT_TAG_BIT	7
#define COUNTER_BIT	3
#define PE_LINES	512
#define PE_WAYS		2
#define PRIME1		509
#define PRIME2		257
#define PRIME3		251
#define PF_THRESH	50
#define LA_THRESH	75

// ============================================================================
/// Definitions for Log, Debug, Warning, Error and Statistics
// ============================================================================
#define FAIL 0                  /// FAIL when return is int32_t or uint32_t
#define OK 1                    /// OK when return is int32_t or uint32_t

#define TRACE_LINE_SIZE 512

/// DETAIL DESCRIPTION: Almost all errors and messages use this definition.
/// It will DEACTIVATE all the other messages below
#define ORCS_PRINTF(...) printf(__VA_ARGS__);

// ~ #define ORCS_DEBUG
#ifdef ORCS_DEBUG
    #define DEBUG_PRINTF(...) {\
                                  ORCS_PRINTF("DEBUG: ");\
                                  ORCS_PRINTF(__VA_ARGS__);\
                              }
#else
    #define DEBUG_PRINTF(...)
#endif



#define ERROR_INFORMATION() {\
                                ORCS_PRINTF("ERROR INFORMATION\n");\
                                ORCS_PRINTF("ERROR: File: %s at Line: %u\n", __FILE__, __LINE__);\
                                ORCS_PRINTF("ERROR: Function: %s\n", __PRETTY_FUNCTION__);\
                                ORCS_PRINTF("ERROR: Cycle: %" PRIu64 "\n", orcs_engine.get_global_cycle());\
                            }


#define ERROR_ASSERT_PRINTF(v, ...) if (!(v)) {\
                                        ERROR_INFORMATION();\
                                        ORCS_PRINTF("ERROR_ASSERT: %s\n", #v);\
                                        ORCS_PRINTF("\nERROR: ");\
                                        ORCS_PRINTF(__VA_ARGS__);\
                                        exit(EXIT_FAILURE);\
                                    }

#define ERROR_PRINTF(...) {\
                              ERROR_INFORMATION();\
                              ORCS_PRINTF("\nERROR: ");\
                              ORCS_PRINTF(__VA_ARGS__);\
                              exit(EXIT_FAILURE);\
                          }


// ==============================================================================
/// Enumerations
// ==============================================================================

// ============================================================================
/// Enumerates the INSTRUCTION (Opcode and Uop) operation type
enum instruction_operation_t {
    /// NOP
    INSTRUCTION_OPERATION_NOP,
    /// INTEGERS
    INSTRUCTION_OPERATION_INT_ALU,
    INSTRUCTION_OPERATION_INT_MUL,
    INSTRUCTION_OPERATION_INT_DIV,
    /// FLOAT POINT
    INSTRUCTION_OPERATION_FP_ALU,
    INSTRUCTION_OPERATION_FP_MUL,
    INSTRUCTION_OPERATION_FP_DIV,
    /// BRANCHES
    INSTRUCTION_OPERATION_BRANCH,
    /// MEMORY OPERATIONS
    INSTRUCTION_OPERATION_MEM_LOAD,
    INSTRUCTION_OPERATION_MEM_STORE,
    /// NOT IDENTIFIED
    INSTRUCTION_OPERATION_OTHER,
    /// SYNCHRONIZATION
    INSTRUCTION_OPERATION_BARRIER,
    /// HMC
    INSTRUCTION_OPERATION_HMC_ROA,     //#12 READ+OP +Answer
    INSTRUCTION_OPERATION_HMC_ROWA     //#13 READ+OP+WRITE +Answer
};

// ============================================================================
/// Enumerates the types of branches
enum branch_t {
    BRANCH_SYSCALL,
    BRANCH_CALL,
    BRANCH_RETURN,
    BRANCH_UNCOND,
    BRANCH_COND
};




/// Our Includes
#include "./simulator.hpp"
#include "./orcs_engine.hpp"
#include "./trace_reader.hpp"
#include "./opcode_package.hpp"

#include "./processor.hpp"



#endif  // _ORCS_ORCS_HPP_
