#include "simulator.hpp"

static unsigned int get_btb_idx(btb_line *btb, uint64_t addr) {
	/// Set Number taken from bits 2-8 on the address
	unsigned int set_number = (int) ((addr >> 2) & 0x7F);
	/// Line that the set starts
	unsigned int btb_base_line = set_number * BTB_WAYS;
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

static unsigned int get_cache_idx(cache_line *L, uint64_t addr, int level) {
	uint64_t tag = addr >> 6;
	unsigned int set_number = (int) ((tag) & (level == 1 ? L1_SET_MASK : L2_SET_MASK));
	unsigned int cache_base_line = set_number * (level == 1 ? L1_WAYS : L2_WAYS);
	unsigned int line = 0;
	uint64_t min_clock = 0xFFFFFFFFFFFFFFFF;
	unsigned int begin = cache_base_line;
	unsigned int end = cache_base_line + (level == 1 ? L1_WAYS : L2_WAYS);
	for(unsigned int i = begin; i < end; i++) {
		if (!L[i].valid) return i;
		else {
		   	if (L[i].clock < min_clock) {
				line = i;
				min_clock = L[i].clock;
			}
		}
	}
	return line;
}

static int find_on_cache(cache_line *L, uint64_t addr, int level) {
	uint64_t tag = addr >> 6;
	unsigned int set_number = (int) ((tag) & (level == 1 ? L1_SET_MASK : L2_SET_MASK));
	unsigned int cache_base_line = set_number * (level == 1 ? L1_WAYS : L2_WAYS);
	unsigned int begin = cache_base_line;
	unsigned int end = cache_base_line + (level == 1 ? L1_WAYS : L2_WAYS);
	for(unsigned int i = begin; i < end; i++) {
		if (L[i].tag == tag && L[i].valid) return i;
	}
	return -1;
}

void processor_t:: get_l1(uint64_t addr) {
	orcs_engine.global_cycle++;
	uint64_t tag = addr >> 6;
	int idx = find_on_cache(l1, addr, 1);
	if (idx != -1 && tag == l1[idx].tag) {
		if (l1[idx].valid) {
			hit_l1++;
			l1[idx].clock = orcs_engine.get_global_cycle();
		} else {
			miss_l1++;
			get_l2(addr);
			idx = get_cache_idx(l1, addr, 1);
			l1[idx].tag = tag;
			l1[idx].clock = orcs_engine.get_global_cycle();
			l1[idx].valid = 1;
			l1[idx].dirty = 0;
		}
	} else {
		idx = get_cache_idx(l1, addr, 1);
		miss_l1++;
		if (l1[idx].dirty && l1[idx].valid) {
			write_backs++;
			orcs_engine.global_cycle += 200;
		}
		get_l2(addr);
		l1[idx].tag = tag;
		l1[idx].clock = orcs_engine.get_global_cycle();
		l1[idx].valid = 1;
		l1[idx].dirty = 0;
	}
}

void processor_t:: get_l2(uint64_t addr) {
	orcs_engine.global_cycle += 4;
	uint64_t tag = addr >> 6;
	int idx = find_on_cache(l2, addr, 2);
	if (idx != -1 && tag == l2[idx].tag) {
		if (l2[idx].valid) {
			hit_l2++;
			l2[idx].clock = orcs_engine.get_global_cycle();
		} else {
			idx = get_cache_idx(l2, addr, 2);
			miss_l2++;
			orcs_engine.global_cycle += 200;
			l2[idx].tag = tag;
			l2[idx].clock = orcs_engine.get_global_cycle();
			l2[idx].valid = 1;
			l2[idx].dirty = 0;
		}
	} else {
		idx = get_cache_idx(l2, addr, 2);
		miss_l2++;
		orcs_engine.global_cycle += 200;
		if (l2[idx].dirty && l2[idx].valid) {
			write_backs++;
			orcs_engine.global_cycle += 200;
		}
		l2[idx].tag = tag;
		l2[idx].clock = orcs_engine.get_global_cycle();
		l2[idx].valid = 1;
		l2[idx].dirty = 0;
	}

}

void processor_t::put_l1(uint64_t addr) {
	get_l1(addr);
	int l1_idx = find_on_cache(l1, addr, 1);
	int l2_idx = find_on_cache(l2, addr, 2);
	l1[l1_idx].dirty = 1;
	if (l2_idx != -1) l2[l2_idx].valid = 0;
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
	btb = (btb_line *) malloc(sizeof(btb_line)*BTB_LINES);
	l1 = (cache_line *) malloc(sizeof(cache_line)*L1_LINES);
	l2 = (cache_line *) malloc(sizeof(cache_line)*L2_LINES);
	for (int i = 0; i < BTB_LINES; i++) {
		btb[i].tag = 0;
		btb[i].addr = 0;
		btb[i].clock = 0;
		btb[i].valid = 0;
		btb[i].branch_type = BRANCH_COND;
		btb[i].bht = 0;
	}
	for (int i = 0; i < L1_LINES; i++) {
		l1[i].tag = 0;
		l1[i].clock = 0;
		l1[i].valid = 0;
		l1[i].dirty = 0;
	}
	for(int i = 0; i < L2_LINES; i++) {
		l2[i].tag = 0;
		l2[i].clock = 0;
		l2[i].valid = 0;
		l2[i].dirty = 0;
	}
	miss_btb = 0;
	miss_l1 = 0;
	hit_l1 = 0;
	miss_l2 = 0;
	hit_l2 = 0;
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

		/// Cache
		if (new_instruction.is_read) {
			get_l1(new_instruction.read_address);
		}
		if (new_instruction.is_read2) {
			get_l1(new_instruction.read2_address);
		}
		if (new_instruction.is_write) {
			put_l1(new_instruction.write_address);
		}



	} else if (penalty_count == BTB_PENALTY) {
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
	ORCS_PRINTF("L1: Hits: %lu - Total: %lu - Ratio: %f\n", hit_l1, (hit_l1 + miss_l1), (float) (hit_l1)/(hit_l1 + miss_l1));
	ORCS_PRINTF("L2: Hits: %lu - Total: %lu - Ratio: %f\n", hit_l2, (hit_l2 + miss_l2), (float) (hit_l2)/(hit_l2 + miss_l2));
	ORCS_PRINTF("Write Backs: %lu\n", write_backs);
};

