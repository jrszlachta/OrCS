#include "simulator.hpp"
#include "queue.hpp"
#include <cmath>

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

int processor_t::prediction(uint64_t addr) {
	struct node *n = past_branch->first, *m;
	uint64_t addrn, addrm;
	sum = 0;
	(n && n->next) ? n = n->next : n = NULL;
	(n && n->next) ? m = n->next : m = NULL;
	for (int i = 0; i < TABLES; i++) {
		(n) ? addrn = n->addr : addrn = 0;
		(m) ? addrm = m->addr : addrm = 0;
		/// Xor do history_segment com log(WEIGHTS) bits
		ind->gshare[i] = (((history_segment >> i) & 0x3FF) ^ addr) % WEIGHTS;
		ind->path[i]   = (addrn ^ (addrm << 1)) % WEIGHTS;
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
		for (int i = 0; i < TABLES; i++) {
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
	}
	miss_btb = 0;
	wrong_guess = 0;
	total_branches = 0;
	penalty_count = 0;
	last_idx = 0;
	predicted = 0;
	sum = 0;

	threshold = (uint8_t) floor(1.93*TABLES + TABLES/2);
	history_segment = 0;
	weights = (int **) malloc(sizeof(int *)*TABLES);
	for (int i = 0; i < TABLES; i++) {
		weights[i] = (int *) malloc(sizeof(int)*WEIGHTS);
		for (int j = 0; j < WEIGHTS; j++) {
			weights[i][j] = 0;
		}
	}
	ind = (struct index *) malloc(sizeof(struct index));
	last_ind = (struct index *) malloc(sizeof(struct index));
	ind->gshare = (int *) malloc(sizeof(int)*TABLES);
	ind->path = (int *) malloc(sizeof(int)*TABLES);
	last_ind->gshare = (int *) malloc(sizeof(int)*TABLES);
	last_ind->path = (int *) malloc(sizeof(int)*TABLES);
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
	for (int i = 0; i < TABLES; i++) {
		last_ind->gshare[i] = ind->gshare[i];
		last_ind->path[i] = ind->path[i];
	}
	last_sum = sum;
	last_idx = btb_idx;
	last_instruction = new_instruction;
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

