#include "simulator.hpp"

static unsigned int get_btb_idx(btb_line *btb, uint64_t addr) {
	/// Set Number taken from bits 2-8 on the address
	unsigned int set_number = (int) ((addr >> 2) & 0x7F);
	/// Line that the set starts
	unsigned int btb_base_line = set_number * N_WAY;
	unsigned int line = 0;
	/// Maximum uint64_t value
	uint64_t min_clock = 0xFFFFFFFFFFFFFFFF;
	for (unsigned int i = btb_base_line; i < btb_base_line + 4; i++) {
		if (btb[i].clock < min_clock) {
			/// Address is not on BTB -> take the oldest line on the set
			line = i;
			min_clock = btb[i].clock;
		}
		if (btb[i].tag == addr) {
			/// Address is on BTB -> return line that the address is on BTB
			return i;
		}
	}

	return line;
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
	btb = (btb_line *) malloc(sizeof(btb_line)*ENTRIES);
	for (int i = 0; i < ENTRIES; i++) {
		btb[i].tag = 0;
		btb[i].addr = 0;
		btb[i].clock = 0;
		btb[i].valid = 0;
		btb[i].branch_type = BRANCH_COND;
		btb[i].bht = 0;
	}
	miss_btb = 0;
	wrong_guess = 0;
	total_branches = 0;
	penalty_count = 0;
	last_idx = 0;
	guess = 0;
	begin_clock = orcs_engine.get_global_cycle();
};

// =====================================================================
void processor_t::clock() {

	/// Get the next instruction from the trace
	opcode_package_t new_instruction;
	if (!penalty_count) {
		if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
			orcs_engine.simulator_alive = false;
		}

		int btb_idx = get_btb_idx(btb, new_instruction.opcode_address);

		if(new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) {
			total_branches++;
			if (new_instruction.opcode_address != btb[btb_idx].tag) {
				/// Branch is not on BTB - Store branch info
				/// Guess = Always Taken
				penalty_count++;
				miss_btb++;
				btb[btb_idx].tag = new_instruction.opcode_address;
				btb[btb_idx].addr = 0;
				btb[btb_idx].clock = orcs_engine.get_global_cycle();
				btb[btb_idx].valid = 1;
				btb[btb_idx].branch_type = new_instruction.branch_type;
				btb[btb_idx].bht = 1;
				guess = btb[btb_idx].bht;
			} else {
				/// Branch is on BTB
				/// Guess = BHT 1 Bit
				guess = btb[btb_idx].bht;
			}

			last_idx = btb_idx;
			last_instruction = new_instruction;
		} else {
			if (last_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH && last_instruction.branch_type == BRANCH_COND) {
				/// If last instruction was an conditional branch, check if it was taken or not
				if (last_instruction.opcode_address
						+ last_instruction.opcode_size
						== new_instruction.opcode_address) {
					/// Branch not taken
					btb[last_idx].bht = 0;
					if (guess == 1) {
						/// Wrong guess generates penalty
						penalty_count++;
						wrong_guess++;
					}
				} else {
					/// Branch taken
					btb[last_idx].bht = 1;
					if (guess == 0) {
						/// Wrong guess generates penalty
						penalty_count++;
						wrong_guess++;
					}
				}
			}
			last_idx = btb_idx;
			last_instruction = new_instruction;
		}
	} else if (penalty_count == PENALTY) {
		/// After 8 cycles the processor is able to get a new instruction
		penalty_count = 0;
   	} else {
		penalty_count++;
	}
};

// =====================================================================
void processor_t::statistics() {
	end_clock = orcs_engine.get_global_cycle();
	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");
	ORCS_PRINTF("%lu Cycles\n", end_clock - begin_clock);
	ORCS_PRINTF("Misses: %lu - Total: %lu\n", miss_btb, total_branches);
	ORCS_PRINTF("Hits BTB: %f%%\n", (float) (total_branches-miss_btb)/total_branches*100);
	ORCS_PRINTF("Wrong Guesses: %lu\n", wrong_guess);
	ORCS_PRINTF("Hits BHT: %f%%\n", (float) (total_branches-wrong_guess)/total_branches*100);

};

