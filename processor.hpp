// ============================================================================
// ============================================================================
struct btb_line {
    uint64_t tag;
    uint64_t addr;
    uint64_t clock;
    int valid;
    branch_t branch_type;
    int bht;
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
		cache_line *l1;
		cache_line *l2;
		int penalty_count;
		int guess;
		unsigned int last_idx;
		opcode_package_t last_instruction;

		uint64_t miss_btb;
		uint64_t wrong_guess;
		uint64_t total_branches;

		uint64_t miss_l1;
		uint64_t miss_l2;

		uint64_t begin_clock;
		uint64_t end_clock;
		// ====================================================================
		/// Methods
		// ====================================================================
		processor_t();
	    void allocate();
	    void clock();
		void get_l1(uint64_t addr);
		void get_l2(uint64_t addr);
		void put_l1(uint64_t addr);
	    void statistics();
};
