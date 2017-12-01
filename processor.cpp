#include "simulator.hpp"
#include "queue.hpp"
#include <cmath>

#define PREFECTH	1
#define LOOKAHEAD	1

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
				l2[idx].ready = 0;
			}
		} else {
			idx = get_cache_idx(l2, addr, 2);
			if (ready != 0) {
				l2[idx].ready = ready;
				l2[idx].prefetched = 1;
			} else {
				miss_l2++;
				orcs_engine.global_cycle += 200;
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
#if PREFECTH
	if (ready == 0)
		try_prefetch(addr);
#endif
}

void processor_t::put_l1(uint64_t addr) {
	get_l1(addr);
	int l1_idx = find_on_cache(l1, addr, 1);
	int l2_idx = find_on_cache(l2, addr, 2);
	l1[l1_idx].dirty = 1;
	if (l2_idx != -1) l2[l2_idx].valid = 0;
}

static int st_idx(st_line *st, uint64_t addr) {
	uint64_t page = addr >> 12;
	int set = page % PRIME1;
	uint64_t tag = page && ((1 << ST_TAG_BIT) - 1);
	uint64_t min_clock = 0xFFFFFFFFFFFFFFFF;
	int begin = set * ST_WAYS;
	int end = begin + ST_WAYS;
	int idx = 0;
	for (int i = begin; i < end; i++) {
		if (st[i].valid && st[i].tag == tag) return i;
	}
	for (int i = begin; i < end; i++) {
		if (!st[i].valid) return i;
		if (st[i].clock < min_clock) {
			idx = i;
			min_clock = st[i].clock;
		}
	}
	return idx;
}

static int pt_find_max(pt_line *pt, uint32_t signature, float *confidence, int mode) {
	// mode: 0 -> Prefetch, 1 -> Look Ahead
	int begin = signature * PT_WAYS;
	int end = begin + PT_WAYS;
	float max = 0, prob = 0;
	int idx = -1;
	for (int i = begin; i < end; i++) {
		if (pt[i].valid) prob = *confidence * pt[i].counter/((float)(1 << COUNTER_BIT));
		if (prob > max) {
			max = prob;
			idx = i;
		}
	}
	if (max*100 > (mode == 0 ? PF_THRESH : LA_THRESH)) {
		*confidence = max;
		return idx;
	}
	return -1;
}

static int pe_idx(pe_line *pe, uint64_t addr) {
	uint64_t page = addr >> 12;
	int set = page % PRIME2;
	if (set > 255) set = page % PRIME3;
	uint64_t tag = page && ((1 << ST_TAG_BIT) - 1);
	uint64_t min_clock = 0xFFFFFFFFFFFFFFFF;
	int begin = set * PT_WAYS;
	int end = begin + PT_WAYS;
	int idx = 0;
	for (int i = begin; i < end; i++) {
		if (pe[i].valid && pe[i].tag == tag) return i;
	}
	for (int i = begin; i < end; i++) {
		if (!pe[i].valid) return i;
		if (pe[i].clock < min_clock) {
			idx = i;
			min_clock = pe[i].clock;
		}
	}
	return idx;
}

uint32_t processor_t::train_st(uint64_t addr) {
	int idx = st_idx(st, addr);
	uint32_t tag = ((addr >> 12) & ((1 << ST_TAG_BIT) - 1));
	uint32_t block = ((addr >> 6) & 0x3F);
	uint32_t sig;
	if (st[idx].valid && st[idx].tag == tag) { // HIT
		int stride = block - st[idx].last_block;
		sig = 0;
		if (stride != 0) {
			update_pt(st[idx].signature, stride);
			sig = (((st[idx].signature << SIG_SHIFT) ^ (stride & SIG_SHIFT)) & ((1 << SIG_LENGTH) - 1));
			st[idx].signature = sig;
		}
		st[idx].clock = orcs_engine.get_global_cycle();
		st[idx].last_block = block;
		return sig;
	} else { // MISS or INVALID
		sig = block;
		st[idx].clock = orcs_engine.get_global_cycle();
		st[idx].tag = tag;
		st[idx].last_block = block;
		st[idx].signature = sig;
		st[idx].valid = 1;
		return 0;
	}
}

void processor_t::update_pt(uint32_t signature, int stride) {
	int set = signature;
	int begin = set * PT_WAYS;
	int end = begin + PT_WAYS;
	int idx = 0;
	int min_counter = (1 << COUNTER_BIT) - 1;
	for (int i = begin; i < end; i++) {
		if (pt[i].valid && pt[i].stride == stride) { // HIT
			if (pt[i].counter < (1 << COUNTER_BIT))
				pt[i].counter++;
			return;
		} else if (!pt[i].valid) { // INVALID
			pt[i].stride = stride;
			pt[i].valid = 1;
			pt[i].counter = 0;
			return;
		}
		if (pt[i].counter < min_counter) {
			idx = i;
			min_counter = pt[i].counter;
		}
	}
	// MISS
	pt[idx].stride = stride;
	pt[idx].valid = 1;
	for (int i = begin; i < end; i++) {
		if (pt[i].counter > 0)
			pt[i].counter--;
	}
	pt[idx].counter = 0;
}

void processor_t::prefetch(uint64_t addr, int stride) {
	int idx = pe_idx(pe, addr);
	uint32_t tag = ((addr >> 12) & ((1 << ST_TAG_BIT) - 1));
	uint64_t block = (addr >> 6);
	if (pe[idx].valid && pe[idx].tag == tag) {
		pe[idx].clock = orcs_engine.get_global_cycle();
		uint64_t prefetch_addr = (block + stride) << 6;
		total_prefetches++;
		get_l2(prefetch_addr, orcs_engine.get_global_cycle()+200);
	} else {
		pe[idx].tag = tag;
		pe[idx].valid = 1;
		pe[idx].clock = orcs_engine.get_global_cycle();
	}
}

void processor_t::look_ahead(uint64_t addr, int stride) {
	int idx = pe_idx(pe, addr);
	uint32_t tag = ((addr >> 12) & ((1 << ST_TAG_BIT) - 1));
	uint64_t block = (addr >> 6);
	if (pe[idx].valid && pe[idx].tag == tag) {
		uint64_t look_ahead_addr = (block + stride) << 6;
		total_prefetches++;
		get_l2(look_ahead_addr, orcs_engine.get_global_cycle()+400);
	}
}

void processor_t::try_prefetch(uint64_t addr) {
	float *confidence = (float *) malloc(sizeof(float));
	*confidence = 1.0f;
	uint32_t signature = train_st(addr);
	int pf_idx = pt_find_max(pt, signature, confidence, 0); // Mode Prefetch
	if (pf_idx >= 0) { // > 50%
		int pf_stride = pt[pf_idx].stride;
		prefetch(addr, pf_stride);
#if LOOKAHEAD
		uint32_t la_signature = ((signature << SIG_SHIFT) ^ (pf_stride & SIG_SHIFT)) & ((1 << SIG_LENGTH) - 1);
		int la_idx = pt_find_max(pt, la_signature, confidence, 1);
		if (la_idx >= 0) { // > 75%
			int la_stride = pt[la_idx].stride;
			look_ahead(addr, la_stride+pf_stride);
		}
#endif
	}
	free(confidence);
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
	btb = (btb_line *) malloc(sizeof(btb_line)*BTB_LINES);
	l1 = (cache_line *) malloc(sizeof(cache_line)*L1_LINES);
	l2 = (cache_line *) malloc(sizeof(cache_line)*L2_LINES);
	st = (st_line *) malloc(sizeof(st_line)*ST_LINES);
	pt = (pt_line *) malloc(sizeof(pt_line)*PT_LINES);
	pe = (pe_line *) malloc(sizeof(pe_line)*PE_LINES);
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
	for(int i = 0; i < ST_LINES; i++) {
		st[i].clock = 0;
		st[i].tag = 0;
		st[i].last_block = 0;
		st[i].signature = 0;
		st[i].valid = 0;
	}
	for(int i = 0; i < PT_LINES; i++) {
		pt[i].valid = 0;
		pt[i].stride = 0;
		pt[i].counter = 0;
	}
	for (int i = 0; i < PE_LINES; i++) {
		pe[i].clock = 0;
		pe[i].tag = 0;
		pe[i].valid = 0;
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
	opcode_package_t new_instruction;
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
			//guess = prediction(new_instruction.opcode_address);
		}
	}
	if (last_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH && last_instruction.branch_type == BRANCH_COND) {
		/// If last instruction was an conditional branch, check if it was taken or not
		if (last_instruction.opcode_address
				+ last_instruction.opcode_size
				== new_instruction.opcode_address) {
			/// Branch not taken
			//update(0);
			history_segment = (history_segment << 1);
			if (predicted == 1) {
				/// Wrong guess generates penalty
				//orcs_engine.global_cycle += 8;
				wrong_guess++;
			}
		} else {
			/// Branch taken
			//update(1);
			history_segment = (history_segment << 1) | 1;
			if (predicted == 0) {
				/// Wrong guess generates penalty
				//orcs_engine.global_cycle += 8;
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

