#ifndef TOMAHAWK_TOMAHAWKCALCPARAMETERS_H_
#define TOMAHAWK_TOMAHAWKCALCPARAMETERS_H_

#include "../io/BasicWriters.h"

namespace Tomahawk{

#define CALC_DEFAULT_MINR2 0.1
#define CALC_DEFAULT_MAXR2 1
#define CALC_DEFAULT_MINP  1e-4
#define CALC_DEFAULT_MINALLELES 5
#define CALC_DEFAULT_MAXALLELES std::numeric_limits<U64>::max()

struct TomahawkCalcParameters{
	typedef TomahawkCalcParameters self_type;
	typedef IO::GenericWriterInterace writer_type;
	enum force_method {none, phasedFunction, unphasedFunction};

	TomahawkCalcParameters() :
		n_threads(std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 1),
		n_chunks(1),
		chunk_selected(0),
		R2_min(CALC_DEFAULT_MINR2 - Constants::ALLOWED_ROUNDING_ERROR),
		R2_max(CALC_DEFAULT_MAXR2 + Constants::ALLOWED_ROUNDING_ERROR),
		P_threshold(CALC_DEFAULT_MINP),
		minimum_alleles(CALC_DEFAULT_MINALLELES),
		maximum_alleles(CALC_DEFAULT_MAXALLELES),
		compression_type(writer_type::compression::binary),
		output_stream_type(writer_type::type::cout),
		force(force_method::none),
		detailed_progress(false)
	{}

	TomahawkCalcParameters(const self_type& other):
		n_threads(other.n_threads),
		n_chunks(other.n_chunks),
		chunk_selected(other.chunk_selected),
		R2_min(other.R2_min),
		R2_max(other.R2_max),
		P_threshold(other.P_threshold),
		minimum_alleles(other.minimum_alleles),
		maximum_alleles(other.maximum_alleles),
		compression_type(other.compression_type),
		output_stream_type(other.output_stream_type),
		force(other.force),
		detailed_progress(other.detailed_progress)
	{}

	~TomahawkCalcParameters(){}

	bool Validate(void);

	friend std::ostream& operator<<(std::ostream& os, const self_type& p){
		// Todo: extend to dump all implicit parameters and store in TWO header output
		os << Helpers::timestamp("CALC", "PARAMETERS") << "R-squared (" << p.R2_min << '-' << p.R2_max << "), P < " << p.P_threshold << ", non-refs > " << p.minimum_alleles;
		return(os);
	}

	S32 n_threads;
	S32 n_chunks;
	S32 chunk_selected;
	double R2_min;
	double R2_max;
	double P_threshold;
	int64_t minimum_alleles;
	int64_t maximum_alleles;
	writer_type::compression compression_type;
	writer_type::type output_stream_type;
	force_method force;
	bool detailed_progress;
};

bool TomahawkCalcParameters::Validate(void){
	if(n_threads < 0){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid number of threads..." << std::endl;
		return false;
	}

	if(n_chunks < 0){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid number of partitions..." << std::endl;
		return false;
	}

	if(chunk_selected < 0 || chunk_selected > n_chunks){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid selected partition..." << std::endl;
		return false;
	}

	if(R2_min < 0 || R2_min > 1){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid minimum R-squared cutoff..." << std::endl;
		return false;
	}

	if(R2_max < 0 || R2_max > 1){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid maximum R-squared cutoff..." << std::endl;
		return false;
	}

	if(R2_min > R2_max){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Minimum R-squared value > maximum R-squared value..." << std::endl;
		return false;
	}

	if(P_threshold < 0 || P_threshold > 1){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid P-value cutoff..." << std::endl;
		return false;
	}

	if(minimum_alleles < 0){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid minimum number of alleles..." << std::endl;
		return false;
	}

	if(maximum_alleles < 0){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Invalid maximum number of alleles..." << std::endl;
		return false;
	}

	if(minimum_alleles > maximum_alleles){
		std::cerr << Helpers::timestamp("ERROR", "CALC") << "Minimum number of alleles > maximum number of alleles..." << std::endl;
		return false;
	}

	this->R2_min -= Constants::ALLOWED_ROUNDING_ERROR;
	this->R2_max += Constants::ALLOWED_ROUNDING_ERROR;

	return true;
}

}

#endif /* TOMAHAWK_TOMAHAWKCALCPARAMETERS_H_ */
