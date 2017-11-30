#include "simulator.hpp"
#include "queue.hpp"
#include <cmath>

#define PREFECTH 1

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

int processor_t::prediction(uint64_t addr) {
	struct node *n = past_branch->first, *m;
	uint64_t addrn, addrm;
	sum = 0;
	(n && n->next) ? n = n->next : n = NULL;
	(n && n->next) ? m = n->next : m = NULL;
	for (int i = 0; i < PRED_TABLES; i++) {
		(n) ? addrn = n->addr : addrn = 0;
		(m) ? addrm = m->addr : addrm = 0;
		/// Xor do history_segment com log(WEIGHTS) bits
		ind->gshare[i] = (((history_segment >> i) & 0x3FF) ^ addr) % PRED_WEIGHTS;
		ind->path[i]   = (addrn ^ (addrm << 1)) % PRED_WEIGHTS;
		sum += weights[i][ind->gshare[i]] + weights[i][ind->path[i]];
		(n && n->next) ? n = n->next : n = NULL;
		(n && n->next) ? m = n->next : m = NULL;
	}
	dequeue(past_branch);
	if (sum >= 0) return 1;
	return 0;
}

void processor_t::update(int outcome) {
	if (predicted != outcome || abs(last_sum) < threshold) {
		for (int i = 0; i < PRED_TABLES; i++) {
			if (outcome == 1) {
				weights[i][last_ind->gshare[i]] += 1;
				weights[i][last_ind->path[i]] += 1;
			} else {
				weights[i][last_ind->gshare[i]] -= 1;
				weights[i][last_ind->path[i]] -= 1;
			}
		}
	}
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

static int prefetcher_idx(stride_line *s, uint64_t pc) {
	int line = -1;
	uint64_t min_clock = 0xFFFFFFFFFFFFFFFF;
	for (int i = 0; i < STRIDE_LINES; i++) {
		if (s[i].tag == pc) return i;
	}
	for (int i = 0; i < STRIDE_LINES; i++) {
		if (s[i].clock < min_clock) line = i;
	}
	return line;
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
			get_l2(addr, 0);
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
		get_l2(addr, 0);
		l1[idx].tag = tag;
		l1[idx].clock = orcs_engine.get_global_cycle();
		l1[idx].valid = 1;
		l1[idx].dirty = 0;
	}
}

void processor_t:: get_l2(uint64_t addr, uint64_t ready) {
	if (ready == 0) orcs_engine.global_cycle += 4;
	uint64_t tag = addr >> 6;
	int idx = find_on_cache(l2, addr, 2);
	if (idx != -1 && tag == l2[idx].tag) {
		if (l2[idx].valid) {
			if (ready > 0) {
				total_prefetches--;
			} else {
				if (l2[idx].prefetched) {
					if (l2[idx].ready > orcs_engine.get_global_cycle()) {
						uint64_t diff = l2[idx].ready - orcs_engine.get_global_cycle();
						orcs_engine.global_cycle += diff;
						wait_time += diff;
					}
					l2[idx].prefetched = 0;
					used_prefetches++;
				}
				hit_l2++;
				l2[idx].clock = orcs_engine.get_global_cycle();
			}
		} else {
			idx = get_cache_idx(l2, addr, 2);
			if (ready != 0) {
				l2[idx].ready = ready;
				l2[idx].prefetched = 1;
			} else {
				miss_l2++;
				orcs_engine.global_cycle += 200;
				prefetch(addr);
			}
			l2[idx].tag = tag;
			l2[idx].clock = orcs_engine.get_global_cycle();
			l2[idx].valid = 1;
			l2[idx].dirty = 0;
		}
	} else {
		idx = get_cache_idx(l2, addr, 2);
		if (ready != 0) {
			l2[idx].ready = ready;
			l2[idx].prefetched = 1;
		} else {
			miss_l2++;
			orcs_engine.global_cycle += 200;
			prefetch(addr);
		}
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

void processor_t::prefetch(uint64_t addr) {
	uint64_t pc = new_instruction.opcode_address;
	int idx = prefetcher_idx(s, pc);
	if (s[idx].tag == pc) {
		if (s[idx].status == STRIDE_INVALID) {
			s[idx].tag = pc;
			s[idx].clock = orcs_engine.get_global_cycle();
			s[idx].last_addr = addr;
			s[idx].stride = 0;
			s[idx].status = STRIDE_TRAINING;
		} else if (s[idx].status == STRIDE_TRAINING) {
			if (addr - s[idx].last_addr == s[idx].stride) {
				s[idx].status = STRIDE_ACTIVE;
				s[idx].clock = orcs_engine.get_global_cycle();
				s[idx].last_addr = addr;
			} else {
				s[idx].stride = addr - s[idx].last_addr;
				s[idx].clock = orcs_engine.get_global_cycle();
				s[idx].last_addr = addr;
			}
		} else {
			if (addr - s[idx].last_addr == s[idx].stride) {
				total_prefetches++;
				uint64_t to_get = addr + STRIDE_DIST * s[idx].stride;
				get_l2(to_get, orcs_engine.get_global_cycle()+200);
				s[idx].clock = orcs_engine.get_global_cycle();
			} else {
				s[idx].status = STRIDE_INVALID;
			}
		}
	} else {
		s[idx].tag = pc;
		s[idx].clock = orcs_engine.get_global_cycle();
		s[idx].last_addr = addr;
		s[idx].stride = 0;
		s[idx].status = STRIDE_INVALID;
	}
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
	btb = (btb_line *) malloc(sizeof(btb_line)*BTB_LINES);
	l1 = (cache_line *) malloc(sizeof(cache_line)*L1_LINES);
	l2 = (cache_line *) malloc(sizeof(cache_line)*L2_LINES);
	s = (stride_line *) malloc(sizeof(stride_line)*STRIDE_LINES);
	for (int i = 0; i < BTB_LINES; i++) {
		btb[i].tag = 0;
		btb[i].addr = 0;
		btb[i].clock = 0;
		btb[i].valid = 0;
		btb[i].branch_type = BRANCH_COND;
	}
	for (int i = 0; i < L1_LINES; i++) {
		l1[i].tag = 0;
		l1[i].clock = 0;
		l1[i].ready = 0;
		l1[i].valid = 0;
		l1[i].dirty = 0;
		l1[i].prefetched = 0;
	}
	for(int i = 0; i < L2_LINES; i++) {
		l2[i].tag = 0;
		l2[i].clock = 0;
		l2[i].ready = 0;
		l2[i].valid = 0;
		l2[i].dirty = 0;
		l2[i].prefetched = 0;
	}
	for(int i = 0; i < STRIDE_LINES; i++) {
		s[i].tag = 0;
		s[i].clock = 0;
		s[i].last_addr = 0;
		s[i].stride = 0;
		s[i].status = STRIDE_INVALID;
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
	total_prefetches = 0;
	used_prefetches = 0;

	threshold = (uint8_t) floor(1.93*PRED_TABLES + PRED_TABLES/2);
	history_segment = 0;
	weights = (int **) malloc(sizeof(int *)*PRED_TABLES);
	for (int i = 0; i < PRED_TABLES; i++) {
		weights[i] = (int *) malloc(sizeof(int)*PRED_WEIGHTS);
		for (int j = 0; j < PRED_WEIGHTS; j++) {
			weights[i][j] = 0;
		}
	}
	ind = (struct index *) malloc(sizeof(struct index));
	last_ind = (struct index *) malloc(sizeof(struct index));
	ind->gshare = (int *) malloc(sizeof(int)*PRED_TABLES);
	ind->path = (int *) malloc(sizeof(int)*PRED_TABLES);
	last_ind->gshare = (int *) malloc(sizeof(int)*PRED_TABLES);
	last_ind->path = (int *) malloc(sizeof(int)*PRED_TABLES);
	past_branch = new_queue();

	begin_clock = orcs_engine.get_global_cycle();
};

// =====================================================================
void processor_t::clock() {

	/// Get the next instruction from the trace
	int guess = 0;
	last_sum = 0;
	if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
		orcs_engine.simulator_alive = false;
	}

	int btb_idx = get_btb_idx(btb, new_instruction.opcode_address);

	if(new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) {
		total_branches++;
		enqueue(past_branch, new_instruction.opcode_address);
		if (new_instruction.opcode_address != btb[btb_idx].tag) {
			/// Branch is not on BTB - Store branch info
			orcs_engine.global_cycle += 8;
			miss_btb++;
			btb[btb_idx].tag = new_instruction.opcode_address;
			btb[btb_idx].addr = 0;
			btb[btb_idx].clock = orcs_engine.get_global_cycle();
			btb[btb_idx].valid = 1;
			btb[btb_idx].branch_type = new_instruction.branch_type;
			//If not on BTB, needs to load pc+opcode_size
			guess = 0;
		} else {
			/// Branch is on BTB
			/// Guess = BHT 2 Bit
			guess = prediction(new_instruction.opcode_address);
		}
	}
	if (last_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH && last_instruction.branch_type == BRANCH_COND) {
		/// If last instruction was an conditional branch, check if it was taken or not
		if (last_instruction.opcode_address
				+ last_instruction.opcode_size
				== new_instruction.opcode_address) {
			/// Branch not taken
			update(0);
			history_segment = (history_segment << 1);
			if (predicted == 1) {
				/// Wrong guess generates penalty
				orcs_engine.global_cycle += 8;
				wrong_guess++;
			}
		} else {
			/// Branch taken
			update(1);
			history_segment = (history_segment << 1) | 1;
			if (predicted == 0) {
				/// Wrong guess generates penalty
				orcs_engine.global_cycle += 8;
				wrong_guess++;
			}
		}
	}
	predicted = guess;
	for (int i = 0; i < PRED_TABLES; i++) {
		last_ind->gshare[i] = ind->gshare[i];
		last_ind->path[i] = ind->path[i];
	}
	last_sum = sum;
	last_idx = btb_idx;
	last_instruction = new_instruction;


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
	ORCS_PRINTF("Prefetches: Used: %lu, Total: %lu, Ratio: %f, Avg: %f\n", used_prefetches, total_prefetches,
		   	(float) (used_prefetches/(float)total_prefetches), (float) (wait_time/(float)used_prefetches));
};

