// ============================================================================
// ============================================================================
struct btb_line {
    uint64_t tag;
    uint64_t addr;
    uint64_t clock;
    int valid;
    branch_t branch_type;
};

struct index {
	int *gshare;
	int *path;
};

struct cache_line {
	uint64_t tag;
	uint64_t clock;
	uint64_t ready;
	int valid;
	int dirty;
	int prefetched;
};

struct st_line {
	uint64_t clock;
	uint32_t tag;
	uint32_t last_block;
	uint32_t signature;
	int valid;
};

struct pt_line {
	int valid;
	int stride;
	int counter;
};

struct pe_line {
	uint64_t clock;
	uint32_t tag;
	int valid;
};

class processor_t {
    private:

    public:
		// BTB
		btb_line *btb;
		int penalty_count;
		unsigned int last_idx;
		opcode_package_t last_instruction;
		uint64_t miss_btb;
		uint64_t total_branches;

		// PREDICTOR
		uint64_t wrong_guess;
		struct queue *past_branch;
		uint32_t history_segment;
		uint8_t threshold;
		struct index *ind, *last_ind;
		int **weights;
		int predicted;
		int sum, last_sum;

		// CACHE
		cache_line *l1;
		cache_line *l2;
		uint64_t miss_l1;
		uint64_t hit_l1;
		uint64_t miss_l2;
		uint64_t hit_l2;
		uint64_t write_backs;

		// PREFETCHER
		st_line *st;
		pt_line *pt;
		pe_line *pe;
		uint64_t total_prefetches;
		uint64_t used_prefetches;
		uint64_t wait_time;

		uint64_t begin_clock;
		uint64_t end_clock;
		// ====================================================================
		/// Methods
		// ====================================================================
		processor_t();
	    void allocate();
	    void clock();
		int prediction(uint64_t addr);
		void update(int outcome);
		void get_l1(uint64_t addr);
		void get_l2(uint64_t addr, uint64_t ready);
		void put_l1(uint64_t addr);
		uint32_t train_st(uint64_t addr);
		void update_pt(uint32_t signature, int stride);
		void prefetch(uint64_t addr, int stride);
		void look_ahead(uint64_t addr, int stride);
		void try_prefetch(uint64_t addr);
	    void statistics();
};
