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
	int valid;
	int dirty;
};

class processor_t {
    private:

    public:
		btb_line *btb;
		int penalty_count;
		unsigned int last_idx;
		opcode_package_t last_instruction;

		uint64_t miss_btb;
		uint64_t wrong_guess;
		uint64_t total_branches;

		struct queue *past_branch;
		uint32_t history_segment;
		uint8_t threshold;
		struct index *ind, *last_ind;
		int **weights;
		int predicted;
		int sum, last_sum;

		cache_line *l1;
		cache_line *l2;

		uint64_t miss_l1;
		uint64_t hit_l1;
		uint64_t miss_l2;
		uint64_t hit_l2;
		uint64_t write_backs;

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
		void get_l2(uint64_t addr);
		void put_l1(uint64_t addr);
	    void statistics();
};
