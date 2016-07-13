//
//  program_options.cpp
//  Octopus
//
//  Created by Daniel Cooke on 27/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "program_options.hpp"

#include <string>
#include <iostream>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <functional>
#include <utility>
#include <thread>
#include <sstream>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

#include "genomic_region.hpp"
#include "aligned_read.hpp"
#include "read_filters.hpp"
#include "read_transform.hpp"
#include "read_transformations.hpp"
#include "read_utils.hpp"
#include "read_pipe.hpp"
#include "candidate_generator_builder.hpp"
#include "haplotype_generator.hpp"
#include "variant_caller_builder.hpp"
#include "variant_caller_factory.hpp"
#include "vcf_reader.hpp"
#include "vcf_writer.hpp"
#include "mappable_algorithms.hpp"
#include "string_utils.hpp"
#include "append.hpp"
#include "phred.hpp"
#include "maths.hpp"
#include "logging.hpp"

namespace Octopus
{
    namespace Options
    {
    void conflicting_options(const po::variables_map& vm, const std::string& opt1, const std::string& opt2)
    {
        if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()) {
            throw std::logic_error(std::string("conflicting options '") + opt1 + "' and '" + opt2 + "'.");
        }
    }
    
    void option_dependency(const po::variables_map& vm, const std::string& for_what,
                           const std::string& required_option)
    {
        if (vm.count(for_what) && !vm[for_what].defaulted())
            if (vm.count(required_option) == 0 || vm[required_option].defaulted()) {
                throw std::logic_error(std::string("option '") + for_what
                                       + "' requires option '" + required_option + "'.");
            }
    }
    
    struct ContigPloidy
    {
        GenomicRegion::ContigNameType contig;
        unsigned ploidy;
    };
    
    std::istream& operator>>(std::istream& in, ContigPloidy& result)
    {
        std::string token;
        in >> token;
        
        if (std::count(std::cbegin(token), std::cend(token), '=') != 1) {
            throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
                "contig-ploidies"};
        }
        
        const auto pos = token.find('=');
        
        const auto rhs = token.substr(pos + 1);
        
        try {
            using PloidyType = decltype(result.ploidy);
            result.ploidy = boost::lexical_cast<PloidyType>(rhs);
        } catch (const boost::bad_lexical_cast&) {
            throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
                "contig-ploidies"};
        }
        
        token.erase(pos);
        
        result.contig = std::move(token);
        
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const ContigPloidy contig_ploidy)
    {
        out << contig_ploidy.contig << "=" << contig_ploidy.ploidy;
        return out;
    }
    
    enum class RefCallType { Positional, Blocked };
    
    std::istream& operator>>(std::istream& in, RefCallType& result)
    {
        std::string token;
        in >> token;
        if (token == "Positional")
            result = RefCallType::Positional;
        else if (token == "Blocked")
            result = RefCallType::Blocked;
        else throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
            "refcalls"};
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const RefCallType& type)
    {
        switch (type) {
            case RefCallType::Positional:
                out << "Positional";
                break;
            case RefCallType::Blocked:
                out << "Blocked";
                break;
        }
        return out;
    }
    
    std::istream& operator>>(std::istream& in, ContigOutputOrder& result)
    {
        std::string token;
        in >> token;
        if (token == "LexicographicalAscending")
            result = ContigOutputOrder::LexicographicalAscending;
        else if (token == "LexicographicalDescending")
            result = ContigOutputOrder::LexicographicalDescending;
        else if (token == "ContigSizeAscending")
            result = ContigOutputOrder::ContigSizeAscending;
        else if (token == "ContigSizeDescending")
            result = ContigOutputOrder::ContigSizeDescending;
        else if (token == "AsInReference")
            result = ContigOutputOrder::AsInReferenceIndex;
        else if (token == "AsInReferenceReversed")
            result = ContigOutputOrder::AsInReferenceIndexReversed;
        else if (token == "Unspecified")
            result = ContigOutputOrder::Unspecified;
        else throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
            "contig-output-order"};
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const ContigOutputOrder& order)
    {
        switch (order) {
            case ContigOutputOrder::LexicographicalAscending:
                out << "LexicographicalAscending";
                break;
            case ContigOutputOrder::LexicographicalDescending:
                out << "LexicographicalDescending";
                break;
            case ContigOutputOrder::ContigSizeAscending:
                out << "ContigSizeAscending";
                break;
            case ContigOutputOrder::ContigSizeDescending:
                out << "ContigSizeDescending";
                break;
            case ContigOutputOrder::AsInReferenceIndex:
                out << "AsInReferenceIndex";
                break;
            case ContigOutputOrder::AsInReferenceIndexReversed:
                out << "AsInReferenceIndexReversed";
                break;
            case ContigOutputOrder::Unspecified:
                out << "Unspecified";
                break;
        }
        return out;
    }
    
    enum class PhasingLevel { Minimal, Conservative, Aggressive };
    
    std::istream& operator>>(std::istream& in, PhasingLevel& result)
    {
        std::string token;
        in >> token;
        if (token == "Minimal")
            result = PhasingLevel::Minimal;
        else if (token == "Conservative")
            result = PhasingLevel::Conservative;
        else if (token == "Aggressive")
            result = PhasingLevel::Aggressive;
        else throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
            "phasing-level"};
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const PhasingLevel& level)
    {
        switch (level) {
            case PhasingLevel::Minimal:
                out << "Minimal";
                break;
            case PhasingLevel::Conservative:
                out << "Conservative";
                break;
            case PhasingLevel::Aggressive:
                out << "Aggressive";
                break;
        }
        return out;
    }
    
    void check_reads_present(const po::variables_map& vm)
    {
        if (vm.count("reads") == 0 && vm.count("reads-file") == 0) {
            throw po::required_option {"--reads | --reads-file"};
        }
    }
    
    void check_region_files_consistent(const po::variables_map& vm)
    {
        if (vm.count("regions-file") == 1 && vm.count("skip-regions-file") == 1) {
            const auto regions_file = vm.at("regions-file").as<std::string>();
            const auto skip_regions_file = vm.at("skip-regions-file").as<std::string>();
            if (regions_file == skip_regions_file) {
                throw std::invalid_argument {"options 'regions-file' and 'skip-regions-file' must"
                    " have unique values"};
            }
        }
    }
    
    void check_trio_consistent(const po::variables_map& vm)
    {
        if (vm.at("caller").as<std::string>() == "trio"
            && (vm.count("maternal-sample") == 0 || vm.count("paternal-sample") == 0)) {
            throw std::logic_error {"option 'maternal-sample' and 'paternal-sample' are required"
                " when caller=trio"};
        }
    }
    
    void validate_caller(const po::variables_map& vm)
    {
        if (vm.count("caller") == 1) {
            const auto caller = vm.at("caller").as<std::string>();
            
            static const std::vector<std::string> valid_callers {
                "individual", "population", "cancer", "trio"
            };
            
            if (std::find(std::cbegin(valid_callers), std::cend(valid_callers), caller)
                == std::cend(valid_callers)) {
                po::validation_error {po::validation_error::kind_t::invalid_option_value, caller,
                    "caller"};
            }
        }
    }
    
    void validate_options(const po::variables_map& vm)
    {
        check_reads_present(vm);
        check_region_files_consistent(vm);
        check_trio_consistent(vm);
        validate_caller(vm);
    }
    
    boost::optional<po::variables_map> parse_options(int argc, const char** argv)
    {
        try {
            po::positional_options_description p;
            
            p.add("caller", -1);
            
            po::options_description general("General");
            general.add_options()
            ("help,h", "Produce help message")
            
            ("version", "Output the version number")
            
            ("config", "A config file, used to populate command line options")
            
            ("debug",
             po::bool_switch()->default_value(false),
             "Writes verbose debug information to debug.log in the working directory")
            
            ("trace",
             po::bool_switch()->default_value(false),
             "Writes very verbose debug information to trace.log in the working directory")
            ;
            
            po::options_description backend("Backend");
            backend.add_options()
            ("working-directory,wd",
             po::value<std::string>(),
             "Sets the working directory")
            
            ("threads,t",
             po::value<unsigned>()->default_value(1),
             "Maximum number of threads to be used, setting to 0 (recommended) lets the application"
             " decide the number of threads ands enables specific algorithm parallelisation")
            
            ("max-reference-cache-footprint,mrcf",
             po::value<float>()->default_value(50),
             "Maximum memory footprint for cached reference sequence (in megabytes)")
            
            ("target-read-buffer-footprint,trbf",
             po::value<float>()->default_value(0.5),
             "None binding request to limit the memory footprint of buffered read data (in gigabytes)")
            
            ("compress-reads,cr",
             po::bool_switch()->default_value(false),
             "Compresses all read data when not being used resulting in a smaller memory footprint"
             " but slower processing")
            
            ("max-open-read-files,morf",
             po::value<unsigned>()->default_value(250),
             "Limits the number of read files that can be open simultaneously")
            ;
            
            po::options_description input("Input/output");
            input.add_options()
            ("reference,R",
             po::value<std::string>()->required(),
             "FASTA format reference genome file to be analysed. Target regions"
             " will be extracted from the reference index if not provded explicitly")
            
            ("reads,r",
             po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of BAM/CRAM files to be analysed."
             " May be specified multiple times")
            
            ("reads-file,rf",
             po::value<std::string>(),
             "File containing a list of BAM/CRAM files, one per line, to be analysed")
            
            ("one-based-indexing,1bi",
             po::bool_switch()->default_value(false),
             "Notifies that input regions are given using one based indexing rather than zero based")
            
            ("regions,T",
             po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of regions (chrom:begin-end) to be analysed."
             " May be specified multiple times")
            
            ("regions-file,TF",
             po::value<std::string>(),
             "File containing a list of regions (chrom:begin-end), one per line, to be analysed")
            
            ("skip-regions,sr",
             po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of regions (chrom:begin-end) to skip"
             " May be specified multiple times")
            
            ("skip-regions-file,srf",
             po::value<std::string>(),
             "File of regions (chrom:begin-end), one per line, to skip")
            
            ("samples,S",
             po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of sample names to analyse")
            
            ("samples-file,SF",
             po::value<std::string>(),
             "File of sample names to analyse, one per line, which must be a subset of the samples"
             " that appear in the read files")
            
            ("output,o",
             po::value<std::string>()->default_value("octopus_calls.vcf"),
             "File to where output is written")
            
            ("contig-output-order,coo",
             po::value<ContigOutputOrder>()->default_value(ContigOutputOrder::AsInReferenceIndex),
             "The order contigs should be written to the output")
            
            ("legacy",
             po::bool_switch()->default_value(false),
             "Outputs a legacy version of the final callset in addition to the native version")
            
            ("regenotype",
             po::value<std::string>(),
             "VCF file specifying calls to regenotype, only sites in this files will appear in the"
             " final output")
            ;
            
            po::options_description transforms("Read transformations");
            transforms.add_options()
            ("disable-all-read-transforms",
             po::bool_switch()->default_value(false),
             "Disables all read transformations")
            
            ("disable-soft-clip-masking",
             po::bool_switch()->default_value(false),
             "Disables soft clipped masking, thus allowing all soft clipped bases to be used"
             " for candidate generation")
            
            ("mask-tails",
             po::value<AlignedRead::SizeType>()->implicit_value(3),
             "Masks this number of bases of the tail of all reads")
            
            ("mask-soft-clipped-boundries",
             po::value<AlignedRead::SizeType>()->default_value(2),
             "Masks this number of adjacent non soft clipped bases when soft clipped bases are present")
            
            ("disable-adapter-masking",
             po::bool_switch()->default_value(false),
             "Disables adapter detection and masking")
            
            ("disable-overlap-masking",
             po::bool_switch()->default_value(false),
             "Disables read segment overlap masking")
            ;
            
            po::options_description filters("Read filtering");
            filters.add_options()
            ("disable-read-filtering",
             po::bool_switch()->default_value(false),
             "Disables all read filters")
            
            ("consider-unmapped-reads,allow-unmapped",
             po::bool_switch()->default_value(false),
             "Allows reads marked as unmapped to be used for calling")
            
            ("min-mapping-quality,min-mq",
             po::value<unsigned>()->default_value(20),
             "Minimum read mapping quality required to consider a read for calling")
            
            ("good-base-quality,good-bq",
             po::value<unsigned>()->default_value(20),
             "Base quality threshold used by min-good-bases and min-good-base-fraction filters")
            
            ("min-good-base-fraction,min-good-bp-frac",
             po::value<double>()->implicit_value(0.5),
             "Base quality threshold used by min-good-bases filter")
            
            ("min-good-bases,min-good-bps",
             po::value<AlignedRead::SizeType>()->default_value(20),
             "Minimum number of bases with quality min-base-quality before read is considered")
            
            ("allow-qc-fails",
             po::bool_switch()->default_value(false),
             "Filters reads marked as QC failed")
            
            ("min-read-length,min-read-len",
             po::value<AlignedRead::SizeType>(),
             "Filters reads shorter than this")
            
            ("max-read-length,max-read-len",
             po::value<AlignedRead::SizeType>(),
             "Filter reads longer than this")
            
            ("allow-marked-duplicates,allow-marked-dups",
             po::bool_switch()->default_value(false),
             "Allows reads marked as duplicate in alignment record")
            
            ("allow-octopus-duplicates,allow-dups",
             po::bool_switch()->default_value(false),
             "Allows reads considered duplicates by Octopus")
            
            ("no-secondary-alignments",
             po::bool_switch()->default_value(false),
             "Filters reads marked as secondary alignments")
            
            ("no-supplementary-alignmenets",
             po::bool_switch()->default_value(false),
             "Filters reads marked as supplementary alignments")
            
            ("consider-reads-with-unmapped-segments",
             po::bool_switch()->default_value(false),
             "Allows reads with unmapped template segmenets to be used for calling")
            
            ("consider-reads-with-distant-segments",
             po::bool_switch()->default_value(false),
             "Allows reads with template segmenets that are on different contigs")
            
            ("allow-adapter-contaminated-reads",
             po::bool_switch()->default_value(false),
             "Allows reads with possible adapter contamination")
            
            ("disable-downsampling,no-downsampling",
             po::bool_switch()->default_value(false),
             "Diables all downsampling")
            
            ("downsample-above",
             po::value<unsigned>()->default_value(500),
             "Downsample reads in regions where coverage is over this")
            
            ("downsample-target",
             po::value<unsigned>()->default_value(400),
             "The target coverage for the downsampler")
            ;
            
            po::options_description candidates("Candidate variant generation");
            candidates.add_options()
            ("disable-raw-cigar-candidate-generator,no-cigar-candidates",
             po::bool_switch()->default_value(false),
             "Disables candidate generation from raw read alignments (CIGAR strings)")
            
            ("disable-assembly-candidate-generator,no-assembly-candidates",
             po::bool_switch()->default_value(false),
             "Disables candidate generation using local re-assembly")
            
            ("candidates-from-source,source",
             po::value<std::string>(),
             "Variant file path containing known variants. These variants will automatically become"
             " candidates")
            
            ("min-base-quality,min-bq",
             po::value<unsigned>()->default_value(20),
             "Only bases with quality above this value are considered for candidate generation")
            
            ("min-supporting-reads,min-support",
             po::value<unsigned>()->implicit_value(2),
             "Minimum number of reads that must support a variant if it is to be considered a candidate."
             " By default Octopus will automatically determine this value")
            
            ("max-variant-size,max-var-size",
             po::value<AlignedRead::SizeType>()->default_value(2000),
             "Maximum candidate varaint size to consider (in region space)")
            
            ("kmer-size,kmer",
             po::value<std::vector<unsigned>>()->multitoken()
                ->default_value(std::vector<unsigned> {10, 25}, "10 25")->composing(),
             "K-mer sizes to use for local re-assembly")
            
            ("min-assembler-base-quality",
             po::value<unsigned>()->default_value(15),
             "Only bases with quality above this value are considered for candidate generation by"
             " the assembler")
            ;
            
            po::options_description caller("Common caller options");
            caller.add_options()
            ("caller,C",
             po::value<std::string>()->default_value("population"),
             "Which of the Octopus callers to use")
            
            ("organism-ploidy,ploidy",
             po::value<unsigned>()->default_value(2),
             "All contigs with unspecified ploidies are assumed the organism ploidy")
            
            ("contig-ploidies",
             po::value<std::vector<ContigPloidy>>()->multitoken(),
             "Space-seperated list of contig=ploidy pairs")
            
            ("contig-ploidies-file",
             po::value<std::string>(),
             "File containing a list of contig=ploidy pairs, one per line")
            
            ("min-variant-posterior,min-post",
             po::value<Phred<double>>()->default_value(Phred<double> {2.0}),
             "Report variant alleles with posterior probability (phred scale) greater than this")
            
            ("min-refcall-posterior,min-ref-post",
             po::value<Phred<double>>()->default_value(Phred<double> {2.0}),
             "Report reference alleles with posterior probability (phred scale) greater than this")
            
            ("report-refcalls,gvcf",
             po::value<RefCallType>()->implicit_value(RefCallType::Blocked),
             "Caller will report reference confidence calls for each position (Positional),"
             " or in automatically sized blocks (Blocked)")
            
            ("sites-only",
             po::bool_switch()->default_value(false),
             "Only outout call sites (i.e. without sample genotype information)")
            
            ("snp-heterozygosity,snp-hets",
             po::value<float>()->default_value(0.001, "0.001"),
             "The germline SNP heterozygosity used to calculate genotype priors")
            
            ("indel-heterozygosity,indel-hets",
             po::value<float>()->default_value(0.0001, "0.0001"),
             "The germline indel heterozygosity used to calculate genotype priors")
            ;
            
            po::options_description cancer("Cancer caller");
            cancer.add_options()
            ("normal-sample,normal",
             po::value<std::string>(),
             "Normal sample - all other samples are considered tumour")
            
            ("somatic-mutation-rate,somatic-rate",
             po::value<float>()->default_value(0.00001, "0.00001"),
             "Expected somatic mutation rate, per megabase pair, for this sample")
            
            ("min-somatic-frequency,min-somatic-freq",
             po::value<float>()->default_value(0.01, "0.01"),
             "minimum allele frequency that can be considered as a viable somatic mutation")
            
            ("credible-mass,cm",
             po::value<float>()->default_value(0.99, "0.99"),
             "Mass of the posterior density to use for evaluating allele frequencies")
            
            ("min-somatic-posterior,min-somatic-post",
             po::value<Phred<double>>()->default_value(Phred<double> {2.0}),
             "Minimum somatic mutation call posterior probability (phred scale)")
            
            ("somatics-only",
             po::bool_switch()->default_value(false),
             "Only report somatic variant calls")
            ;
            
            po::options_description trio("Trio caller");
            trio.add_options()
            ("maternal-sample,mother",
             po::value<std::string>(),
             "Maternal sample")
            
            ("paternal-sample,father",
             po::value<std::string>(),
             "Paternal sample")
            
            ("denovos-only",
             po::bool_switch()->default_value(false),
             "Only report de novo variant calls (i.e. alleles unique to the child)")
            ;
            
            po::options_description phaser("Phasing options");
            phaser.add_options()
            ("phasing-level,phase",
             po::value<PhasingLevel>()->default_value(PhasingLevel::Conservative),
             "Level of phasing - longer range phasing can improve calling accuracy at the cost"
             " of runtime speed. Possible values are: Minimal, Conservative, Aggressive")
            
            ("min-phase-score",
             po::value<Phred<double>>()->default_value(Phred<double> {20.0}),
             "Minimum phase score (phred scale) required to report sites as phased")
            
            ("use-unconditional-phase-score",
             po::bool_switch()->default_value(false),
             "Computes unconditional phase scores rather than conditioning on called genotypes")
            
            ("disable-read-guided-phasing",
             po::bool_switch()->default_value(false),
             "Restricts phase score computation to use only genotype posteriors")
            ;
            
            po::options_description advanced("Advanced calling algorithm");
            advanced.add_options()
            ("max-haplotypes,max-haps",
             po::value<unsigned>()->default_value(128),
             "Maximum number of candidate haplotypes the caller may consider")
            
            ("min-haplotype-filter-posterior,min-hap-post",
             po::value<float>()->default_value(1e-10, "1e-10"),
             "Haplotypes with posterior less than this can be filtered, allowing greater"
             " longer haplotype extesion in complex regions")
            
            ("disable-inactive-flank-scoring,noIFS",
             po::bool_switch()->default_value(false),
             "Disables additional calculation to adjust alignment score when there are inactive"
             " candidates in haplotype flanking regions")
            ;
            
            po::options_description call_filtering("Callset filtering");
            call_filtering.add_options()
            ("disable-call-filtering,no-filtering",
             po::bool_switch()->default_value(false),
             "Disables all callset filtering")
            
            ("disable-model-filtering,noMF",
             po::bool_switch()->default_value(false),
             "Disables model based filtering of variant calls")
            ;
            
            po::options_description all("Octopus options");
            all.add(general).add(backend).add(input).add(transforms).add(filters)
            .add(candidates).add(caller).add(advanced).add(cancer).add(trio).add(phaser)
            .add(call_filtering);
            
            po::variables_map vm_init;
            
            po::store(po::command_line_parser(argc, argv).options(general)
                      .allow_unregistered().run(), vm_init);
            
            if (vm_init.count("help") == 1) {
                po::store(po::command_line_parser(argc, argv).options(caller)
                          .allow_unregistered().run(), vm_init);
                
                if (vm_init.count("caller") == 1) {
                    const auto caller = vm_init.at("caller").as<std::string>();
                    
                    validate_caller(vm_init);
                    
                    if (caller == "individual") {
                        std::cout << all << std::endl;
                    } else if (caller == "population") {
                        std::cout << all << std::endl;
                    } else if (caller == "cancer") {
                        std::cout << all << std::endl;
                    } else {
                        std::cout << all << std::endl;
                    }
                } else {
                    std::cout << all << std::endl;
                }
                
                return vm_init;
            }
            
            if (vm_init.count("version") == 1) {
                std::cout << "Octopus version " << Octopus_version << std::endl;
                return vm_init;
            }
            
            po::variables_map vm;
            
            if (vm_init.count("config") == 1) {
                std::ifstream config {vm.at("config").as<std::string>()};
                if (config) {
                    po::store(po::parse_config_file(config, all), vm);
                }
            }
            
            vm_init.clear();
            
            po::store(po::command_line_parser(argc, argv).options(all).positional(p).run(), vm);
            
            // boost::option cannot handle option dependencies so we must do our own checks
            validate_options(vm);
            
            po::notify(vm);
            
            return vm;
        } catch (const std::exception& e) {
            std::clog << "Option error: " << e.what() << std::endl;
            return boost::none;
        }
    }
    
    bool is_run_command(const po::variables_map& options)
    {
        return options.count("help") == 0 && options.count("version") == 0;
    }
    
    bool is_debug_mode(const po::variables_map& options)
    {
        return options.at("debug").as<bool>();
    }
    
    bool is_trace_mode(const po::variables_map& options)
    {
        return options.at("trace").as<bool>();
    }
    
    namespace
    {
        struct Line
        {
            std::string line_data;
            
            operator std::string() const
            {
                return line_data;
            }
        };
        
        std::istream& operator>>(std::istream& is, Line& data)
        {
            std::getline(is, data.line_data);
            
            if (!data.line_data.empty() && data.line_data.back() == '\r') {
                data.line_data.pop_back();
            }
            
            return is;
        }
    } // namespace
    
    boost::optional<fs::path> get_home_dir()
    {
        static const auto result = fs::path(std::getenv("HOME"));
        
        if (fs::is_directory(result)) {
            return result;
        }
        
        return boost::none;
    }
    
    bool is_shorthand_user_path(const fs::path& path)
    {
        return !path.empty() && path.string().front() == '~';
    }
    
    fs::path expand_user_path(const fs::path& path)
    {
        if (is_shorthand_user_path(path)) {
            if (path.string().size() > 1 && path.string()[1] == '/') {
                const auto home_dir = get_home_dir();
                if (home_dir) {
                    return fs::path {home_dir->string() + path.string().substr(1)};
                }
                std::ostringstream ss {};
                ss << "Unable to expand user path";
                ss << path;
                ss << " as the user home directory cannot be located";
                throw std::runtime_error {ss.str()};
            }
            return path;
        }
        return path;
    }
    
    fs::path get_working_directory(const po::variables_map& options)
    {
        if (options.count("working-directory") == 1) {
            auto result = expand_user_path(options.at("working-directory").as<std::string>());
            
            if (!fs::exists(result) && !fs::is_directory(result)) {
                std::ostringstream ss {};
                ss << "The working directory ";
                ss << result;
                ss << " given in the option (--working-directory) does not exist";
                throw std::runtime_error {ss.str()};
            }
            
            return result;
        }
        return fs::current_path();
    }
    
    fs::path resolve_path(const fs::path& path, const po::variables_map& options)
    {
        if (is_shorthand_user_path(path)) {
            return expand_user_path(path); // must be a root path
        }
        
        if (fs::exists(path)) {
            return path; // must be a root path
        }
        
        const auto parent_dir = path.parent_path();
        
        const auto wd = get_working_directory(options);
        
        if (fs::exists(parent_dir) && fs::is_directory(parent_dir)) {
            auto tmp = wd;
            tmp /= path;
            auto wd_parent = tmp.parent_path();
            if (fs::exists(wd_parent) && fs::is_directory(wd_parent)) {
                return tmp; // prefer working directory in case of name clash
            }
            return path; // must be yet-to-be-created root path
        }
        
        auto result = wd;
        result /= path;
        return result;
    }
    
    std::vector<fs::path>
    extract_paths_from_file(const fs::path& file_path, const po::variables_map& options)
    {
        const auto resolved_path = resolve_path(file_path, options);
        
        std::vector<fs::path> result {};
        
        std::ifstream file {file_path.string()};
        
        if (!file.good()) {
            std::ostringstream ss {};
            ss << "Could not open path file " << file_path;
            throw std::runtime_error {ss.str()};
        }
        
        std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                       std::back_inserter(result), [] (const Line& line) { return line.line_data; });
        
        const auto it = std::remove_if(std::begin(result), std::end(result),
                                       [] (const auto& path) { return path.empty(); });
        
        result.erase(it, std::end(result));
        
        return result;
    }
    
    auto resolve_paths(const std::vector<fs::path>& paths, const po::variables_map& options)
    {
        std::vector<fs::path> good_paths {}, bad_paths {};
        good_paths.reserve(paths.size());
        
        for (const auto& path : paths) {
            try {
                good_paths.push_back(resolve_path(path, options));
            } catch (...) {
                bad_paths.push_back(path);
            }
        }
        
        return std::make_pair(std::move(good_paths), std::move(bad_paths));
    }
    
    auto resolve_paths(const std::vector<std::string>& path_strings,
                           const po::variables_map& options)
    {
        std::vector<fs::path> paths {std::cbegin(path_strings), std::cend(path_strings)};
        return resolve_paths(paths, options);
    }
    
    bool is_file_readable(const fs::path& path)
    {
        std::ifstream tmp {path.string()};
        return tmp.good();
    }
    
    bool is_file_writable(const fs::path& path)
    {
        if (!fs::exists(path.parent_path())) {
            return false;
        }
        
        std::ofstream test {path.string()};
        
        const auto result = test.is_open();
        
        fs::remove(path);
        
        return result;
    }
    
    bool is_threading_allowed(const po::variables_map& options)
    {
        const auto num_threads = options.at("threads").as<unsigned>();
        return num_threads != 1;
    }
    
    boost::optional<unsigned> get_num_threads(const po::variables_map& options)
    {
        const auto num_threads = options.at("threads").as<unsigned>();
        
        if (num_threads > 0) return num_threads;
        
        return boost::none;
    }
    
    std::size_t get_target_read_buffer_size(const po::variables_map& options)
    {
        static constexpr std::size_t scale {1'000'000'000};
        return static_cast<std::size_t>(scale * options.at("target-read-buffer-size").as<float>());
    }
    
    boost::optional<fs::path> get_debug_log_file_name(const po::variables_map& options)
    {
        if (options.at("debug").as<bool>()) {
            return resolve_path("octopus_debug.log", options);
        }
        return boost::none;
    }
    
    boost::optional<fs::path> get_trace_log_file_name(const po::variables_map& options)
    {
        if (options.at("trace").as<bool>()) {
            return resolve_path("octopus_trace.log", options);
        }
        return boost::none;
    }
    
    ReferenceGenome make_reference(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        const fs::path input_path {options.at("reference").as<std::string>()};
        
        auto resolved_path = resolve_path(input_path, options);
        
        if (!fs::exists(resolved_path)) {
            stream(log) << "The path " << input_path
                << " given in the input option (--reference) does not exist";
        }
        
        if (!is_file_readable(resolved_path)) {
            stream(log) << "The path " << input_path
                << " given in the input option (--reference) is not readable";
        }
        
        const auto ref_cache_size = options.at("reference-cache-size").as<float>();
        
        static constexpr unsigned Scale {1'000'000};
        
        return ::make_reference(std::move(resolved_path),
                                static_cast<ReferenceGenome::SizeType>(Scale * ref_cache_size),
                                is_threading_allowed(options));
    }
    
    bool is_bed_file(const fs::path& path)
    {
        return path.extension().string() == ".bed";
    }
    
    void seek_past_bed_header(std::ifstream& bed_file)
    {
        // TODO
    }
    
    std::string convert_bed_line_to_region_str(const std::string& bed_line)
    {
        constexpr static char bed_delim {'\t'};
        
        const auto tokens = split(bed_line, bed_delim);
        
        switch (tokens.size()) {
            case 0:
                throw std::runtime_error {"BadBED: found empty BED record"};
            case 1:
                return std::string {tokens[0]};
            case 2:
                // Assume this represents a half range rather than a position
                return std::string {tokens[0] + ':' + tokens[1] + '-'};
            default:
                return std::string {tokens[0] + ':' + tokens[1] + '-' + tokens[2]};
        }
    }
    
    std::function<GenomicRegion(const std::string&)>
    make_region_line_parser(const fs::path& region_path, const ReferenceGenome& reference)
    {
        if (is_bed_file(region_path)) {
            return [&] (const std::string& line) -> GenomicRegion
            {
                return parse_region(convert_bed_line_to_region_str(line), reference);
            };
        } else {
            return [&] (const std::string& line) { return parse_region(line, reference); };
        }
    }
    
    auto extract_regions_from_file(const fs::path& file_path, const ReferenceGenome& reference)
    {
        std::ifstream file {file_path.string()};
        
        if (is_bed_file(file_path)) {
            seek_past_bed_header(file);
        }
        
        std::deque<GenomicRegion> result {};
        
        std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                       std::back_inserter(result),
                       make_region_line_parser(file_path, reference));
        
        result.shrink_to_fit();
        
        return result;
    }
    
    InputRegionMap make_search_regions(const std::vector<GenomicRegion>& regions)
    {
        InputRegionMap contig_mapped_regions {};
        
        for (const auto& region : regions) {
            contig_mapped_regions[region.contig_name()].insert(region);
        }
        
        InputRegionMap result {};
        result.reserve(contig_mapped_regions.size());
        
        for (const auto& p : contig_mapped_regions) {
            auto covered_contig_regions = extract_covered_regions(p.second);
            result.emplace(std::piecewise_construct,
                           std::forward_as_tuple(p.first),
                           std::forward_as_tuple(std::make_move_iterator(std::begin(covered_contig_regions)),
                                                 std::make_move_iterator(std::end(covered_contig_regions))));
        }
        
        return result;
    }
    
    InputRegionMap extract_search_regions(const ReferenceGenome& reference)
    {
        return make_search_regions(get_all_contig_regions(reference));
    }
    
    MappableFlatSet<GenomicRegion>
    cut(const MappableFlatSet<GenomicRegion>& mappables, const MappableFlatSet<GenomicRegion>& regions)
    {
        if (mappables.empty()) return regions;
        
        MappableFlatSet<GenomicRegion> result {};
        
        for (const auto& region : regions) {
            auto overlapped = mappables.overlap_range(region);
            
            if (empty(overlapped)) {
                result.emplace(region);
            } else if (!is_same_region(region, overlapped.front())) {
                auto spliced = region;
                
                if (begins_before(overlapped.front(), spliced)) {
                    spliced = right_overhang_region(spliced, overlapped.front());
                    overlapped.advance_begin(1);
                }
                
                std::for_each(std::cbegin(overlapped), std::cend(overlapped), [&] (const auto& region) {
                    result.emplace(left_overhang_region(spliced, region));
                    spliced = expand_lhs(spliced, -begin_distance(spliced, region));
                });
                
                if (ends_before(overlapped.back(), spliced)) {
                    result.emplace(right_overhang_region(spliced, overlapped.back()));
                }
            }
        }
        
        result.shrink_to_fit();
        
        return result;
    }
    
    InputRegionMap extract_search_regions(const std::vector<GenomicRegion>& regions,
                                         std::vector<GenomicRegion>& skip_regions)
    {
        auto input_regions = make_search_regions(regions);
        
        const auto skipped = make_search_regions(skip_regions);
        
        InputRegionMap result {input_regions.size()};
        
        for (auto& p : input_regions) {
            if (skipped.count(p.first) == 1) {
                result.emplace(p.first, cut(skipped.at(p.first), std::move(p.second)));
            } else {
                result.emplace(p.first, std::move(p.second));
            }
        }
        
        for (auto it = std::begin(result); it != std::end(result); ) {
            if (it->second.empty()) {
                it = result.erase(it);
            } else {
                ++it;
            }
        }
        
        for (auto& p : result) {
            p.second.shrink_to_fit();
        }
        
        return result;
    }
    
    InputRegionMap extract_search_regions(const ReferenceGenome& reference,
                                         std::vector<GenomicRegion>& skip_regions)
    {
        return extract_search_regions(get_all_contig_regions(reference), skip_regions);
    }
    
    std::vector<GenomicRegion> parse_regions(const std::vector<std::string>& unparsed_regions,
                                             const ReferenceGenome& reference)
    {
        std::vector<GenomicRegion> result {};
        result.reserve(unparsed_regions.size());
        
        bool all_region_parsed {true};
        
        for (const auto& unparsed_region : unparsed_regions) {
            Logging::WarningLogger log {};
            try {
                result.push_back(parse_region(unparsed_region, reference));
            } catch (std::exception& e) {
                all_region_parsed = false;
                stream(log) << "Could not parse input region \"" << unparsed_region
                            << "\". Check the format is correct, the contig is in the reference genome \""
                            << reference.name() << "\", and the coordinate range is in bounds.";
            }
        }
        
        if (!all_region_parsed) {
            result.clear();
            result.shrink_to_fit();
        }
        
        return result;
    }
    
    auto transform_to_zero_based(std::vector<GenomicRegion>&& one_based_regions)
    {
        std::vector<GenomicRegion> result {};
        result.reserve(one_based_regions.size());
        
        for (auto&& region : one_based_regions) {
            if (region.begin() > 0) {
                result.push_back(shift(std::move(region), -1));
            } else {
                result.push_back(std::move(region));
            }
        }
        
        return result;
    }
    
    auto transform_to_zero_based(InputRegionMap::mapped_type&& one_based_regions)
    {
        MappableFlatSet<GenomicRegion> result {};
        
        for (auto&& region : one_based_regions) {
            result.insert(shift(std::move(region), -1));
        }
        
        return result;
    }
    
    auto transform_to_zero_based(InputRegionMap&& one_based_search_regions)
    {
        InputRegionMap result {one_based_search_regions.size()};
        
        for (auto& p : one_based_search_regions) {
            result.emplace(p.first, transform_to_zero_based(std::move(p.second)));
        }
        
        return result;
    }
    
    InputRegionMap get_search_regions(const po::variables_map& options, const ReferenceGenome& reference)
    {
        Logging::ErrorLogger log {};
        
        std::vector<GenomicRegion> skip_regions {};
        
        bool all_parsed {true};
        
        if (options.count("skip-regions") == 1) {
            const auto& region_strings = options.at("skip-regions").as<std::vector<std::string>>();
            auto parsed_regions = parse_regions(region_strings, reference);
            if (region_strings.size() == parsed_regions.size()) {
                append(std::move(parsed_regions), skip_regions);
            } else {
                all_parsed = false;
            }
        }
        
        if (options.count("skip-regions-file") == 1) {
            const auto& input_path = options.at("skip-regions-file").as<std::string>();
            
            auto resolved_path = resolve_path(input_path, options);
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--skip-regions-file) does not exist";
            }else if (!is_file_readable(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--skip-regions-file) is not readable";
            } else {
                append(extract_regions_from_file(resolved_path, reference), skip_regions);
            }
        }
        
        if (options.at("use-one-based-indexing").as<bool>()) {
            skip_regions = transform_to_zero_based(std::move(skip_regions));
        }
        
        if (options.count("regions") == 0 && options.count("regions-file") == 0) {
            if (options.count("regenotype") == 1) {
                // TODO: only extract regions in the regenotype VCF
            }
            return extract_search_regions(reference, skip_regions);
        }
        
        std::vector<GenomicRegion> input_regions {};
        
        if (options.count("regions") == 1) {
            const auto& region_strings = options.at("regions").as<std::vector<std::string>>();
            auto parsed_regions = parse_regions(region_strings, reference);
            if (region_strings.size() == parsed_regions.size()) {
                append(std::move(parsed_regions), input_regions);
            } else {
                all_parsed = false;
            }
        }
        
        if (options.count("regions-file") == 1) {
            const auto& input_path = options.at("regions-file").as<std::string>();
            
            auto resolved_path = resolve_path(input_path, options);
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--skip-regions-file) does not exist";
            } else if (!is_file_readable(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--skip-regions-file) is not readable";
            } else {
                append(extract_regions_from_file(resolved_path, reference), input_regions);
            }
        }
        
        if (!all_parsed) {
            Logging::WarningLogger log {};
            if (!input_regions.empty()) {
                stream(log) << "Detected unparsed input regions so dumping "
                            << input_regions.size() << " parsed regions";
                input_regions.clear();
            }
            skip_regions.clear();
        }
        
        auto result = extract_search_regions(input_regions, skip_regions);
        
        if (options.at("use-one-based-indexing").as<bool>()) {
            return transform_to_zero_based(std::move(result));
        }
        
        return result;
    }
    
    ContigOutputOrder get_contig_output_order(const po::variables_map& options)
    {
        return options.at("contig-output-order").as<ContigOutputOrder>();
    }
    
    boost::optional<std::vector<SampleIdType>> get_user_samples(const po::variables_map& options)
    {
        if (options.count("samples") == 1) {
            return options.at("samples").as<std::vector<SampleIdType>>();
        }
        return boost::none;
    }
    
    namespace
    {
        void log_unresolved_read_paths(const std::vector<fs::path>& paths,
                                       const std::string& option)
        {
            Logging::WarningLogger log {};
            for (const auto& path : paths) {
                stream(log) << "Could not resolve the path " << path
                            << " given in the input option (--" + option +")";
            }
        }
        
        auto parition_existent_paths(std::vector<fs::path>& paths)
        {
            return std::partition(std::begin(paths), std::end(paths),
                                  [] (const auto& path) { return fs::exists(path); });
        }
        
        template <typename InputIt>
        void log_nonexistent_read_paths(InputIt first, InputIt last, const std::string& option)
        {
            Logging::WarningLogger log {};
            std::for_each(first, last, [&option, &log] (const auto& path) {
                stream(log) << "The path " << path
                            << " given in the input option (--" + option + ") does not exist";
            });
        }
        
        auto parition_readable_paths(std::vector<fs::path>& paths)
        {
            return std::partition(std::begin(paths), std::end(paths),
                                  [] (const auto& path) { return is_file_readable(path); });
        }
        
        template <typename InputIt>
        void log_unreadable_read_paths(InputIt first, InputIt last, const std::string& option)
        {
            Logging::WarningLogger log {};
            std::for_each(first, last, [&option, &log] (const auto& path) {
                stream(log) << "The path " << path
                            << " given in the input option (--" + option + ") is not readable";
            });
        }
    } // namespace
    
    boost::optional<std::vector<fs::path>> get_read_paths(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        std::vector<fs::path> result {};
        
        bool all_paths_good {true};
        
        std::vector<fs::path> resolved_paths {}, unresolved_paths {};
        
        if (options.count("reads") == 1) {
            const auto& read_paths = options.at("reads").as<std::vector<std::string>>();
            
            std::tie(resolved_paths, unresolved_paths) = resolve_paths(read_paths, options);
            
            if (!unresolved_paths.empty()) {
                log_unresolved_read_paths(unresolved_paths, "reads");
                all_paths_good = false;
            }
            
            auto it = parition_existent_paths(resolved_paths);
            
            if (it != std::end(resolved_paths)) {
                log_nonexistent_read_paths(it, std::end(resolved_paths), "reads");
                all_paths_good = false;
                resolved_paths.erase(it, std::end(resolved_paths));
            }
            
            it = parition_readable_paths(resolved_paths);
            
            if (it != std::end(resolved_paths)) {
                log_unreadable_read_paths(it, std::end(resolved_paths), "reads");
                all_paths_good = false;
            }
            
            append(std::move(resolved_paths), result);
        }
        
        if (options.count("reads-file") == 1) {
            // first we need to make sure the path to the paths is okay
            const fs::path input_path {options.at("reads-file").as<std::string>()};
            
            auto resolved_path = resolve_path(input_path, options);
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--reads-file) does not exist";
                all_paths_good = false;
            } else if (!is_file_readable(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--reads-file) is not readable";
                all_paths_good = false;
            } else {
                auto paths = extract_paths_from_file(resolved_path, options);
                
                std::tie(resolved_paths, unresolved_paths) = resolve_paths(paths, options);
                
                if (!unresolved_paths.empty()) {
                    log_unresolved_read_paths(unresolved_paths, "reads-file");
                    all_paths_good = false;
                }
                
                auto it = parition_existent_paths(resolved_paths);
                
                if (it != std::end(resolved_paths)) {
                    log_nonexistent_read_paths(it, std::end(resolved_paths), "reads-file");
                    all_paths_good = false;
                    resolved_paths.erase(it, std::end(resolved_paths));
                }
                
                it = parition_readable_paths(resolved_paths);
                
                if (it != std::end(resolved_paths)) {
                    log_unreadable_read_paths(it, std::end(resolved_paths), "reads-file");
                    all_paths_good = false;
                }
                
                append(std::move(resolved_paths), result);
            }
        }
        
        std::sort(std::begin(result), std::end(result));
        
        const auto it = std::unique(std::begin(result), std::end(result));
                         
        const auto num_duplicates = std::distance(it, std::end(result));
        
        if (num_duplicates > 0) {
            Logging::WarningLogger log {};
            stream(log) << "There are " << num_duplicates << " duplicate read paths but only unique paths will be considered";
        }
        
        result.erase(it, std::end(result));
        
        if (!all_paths_good && result.size() > 0) {
            Logging::WarningLogger log {};
            auto slog = stream(log);
            slog << "There are bad read paths so dumping " << result.size() << " good path";
            if (result.size() > 1) slog << "s";
            result.clear();
        }
        
        return result;
    }
    
    ReadManager make_read_manager(const po::variables_map& options)
    {
        auto read_paths = get_read_paths(options);
        
        if (read_paths) {
            const auto max_open_files = options.at("max-open-read-files").as<unsigned>();
            
            return ReadManager {*std::move(read_paths), max_open_files};
        }
        
        throw std::runtime_error {"Unable to load read paths"};
    }
    
    ReadFilterer make_read_filter(const po::variables_map& options)
    {
        using std::make_unique;
        
        using QualityType = AlignedRead::QualityType;
        using SizeType    = AlignedRead::SizeType;
        
        using namespace ReadFilters;
        
        ReadFilterer result {};
        
        // these filters are mandatory
        result.register_filter(make_unique<HasValidQualities>());
        result.register_filter(make_unique<HasWellFormedCigar>());
        
        if (options.at("disable-read-filtering").as<bool>()) {
            return result;
        }
        
        if (!options.at("consider-unmapped-reads").as<bool>()) {
            result.register_filter(make_unique<IsMapped>());
        }
        
        const auto min_mapping_quality = options.at("min-mapping-quality").as<unsigned>();
        
        if (min_mapping_quality > 0) {
            result.register_filter(make_unique<IsGoodMappingQuality>(min_mapping_quality));
        }
        
        const auto min_base_quality = options.at("good-base-quality").as<unsigned>();
        const auto min_good_bases   = options.at("min-good-bases").as<unsigned>();
        
        if (min_base_quality > 0 && min_good_bases > 0) {
            result.register_filter(make_unique<HasSufficientGoodQualityBases>(min_base_quality,
                                                                              min_good_bases));
        }
        
        if (min_base_quality > 0 && options.count("min-good-base-fraction") == 1) {
            auto min_good_base_fraction = options.at("min-good-base-fraction").as<double>();
            result.register_filter(make_unique<HasSufficientGoodBaseFraction>(min_base_quality,
                                                                              min_good_base_fraction));
        }
        
        if (options.count("min-read-length") == 1) {
            result.register_filter(make_unique<IsShort>(options.at("min-read-length").as<SizeType>()));
        }
        
        if (options.count("max-read-length") == 1) {
            result.register_filter(make_unique<IsLong>(options.at("max-read-length").as<SizeType>()));
        }
        
        if (!options.at("allow-marked-duplicates").as<bool>()) {
            result.register_filter(make_unique<IsNotMarkedDuplicate>());
        }
        
        if (!options.at("allow-octopus-duplicates").as<bool>()) {
            result.register_filter(make_unique<IsNotDuplicate<ReadFilterer::BidirIt>>());
        }
        
        if (!options.at("allow-qc-fails").as<bool>()) {
            result.register_filter(make_unique<IsNotMarkedQcFail>());
        }
        
        if (options.at("no-secondary-alignments").as<bool>()) {
            result.register_filter(make_unique<IsNotSecondaryAlignment>());
        }
        
        if (options.at("no-supplementary-alignmenets").as<bool>()) {
            result.register_filter(make_unique<IsNotSupplementaryAlignment>());
        }
        
        if (!options.at("consider-reads-with-unmapped-segments").as<bool>()) {
            result.register_filter(make_unique<IsNextSegmentMapped>());
            result.register_filter(make_unique<IsProperTemplate>());
        }
        
        if (!options.at("consider-reads-with-distant-segments").as<bool>()) {
            result.register_filter(make_unique<IsLocalTemplate>());
        }
        
        if (!options.at("allow-adapter-contaminated-reads").as<bool>()) {
            result.register_filter(make_unique<IsNotContaminated>());
        }
        
        result.shrink_to_fit();
        
        return result;
    }
    
    boost::optional<Downsampler> make_downsampler(const po::variables_map& options)
    {
        if (options.at("disable-downsampling").as<bool>()) {
            return boost::none;
        }
        
        auto max_coverage    = options.at("downsample-above").as<unsigned>();
        auto target_coverage = options.at("downsample-target").as<unsigned>();
        
        return Downsampler(max_coverage, target_coverage);
    }
    
    ReadTransform make_read_transform(const po::variables_map& options)
    {
        using SizeType = AlignedRead::SizeType;
        
        ReadTransform result {};
        
        result.register_transform(ReadTransforms::CapBaseQualities {125});
        
        if (options.at("disable-read-transforms").as<bool>()) {
            return result;
        }
        
        //result.register_transform(ReadTransforms::QualityAdjustedSoftClippedMasker {});
        
        if (options.count("mask-tails")) {
            const auto tail_mask_size = options.at("mask-tails").as<SizeType>();
            
            if (tail_mask_size > 0) {
                result.register_transform(ReadTransforms::MaskTail {tail_mask_size});
            }
        }
        
        if (!options.at("disable-soft-clip-masking").as<bool>()) {
            const auto soft_clipped_mask_size = options.at("mask-soft-clipped-boundries").as<SizeType>();
            
            if (soft_clipped_mask_size > 0) {
                result.register_transform(ReadTransforms::MaskSoftClippedBoundries {soft_clipped_mask_size});
            } else {
                result.register_transform(ReadTransforms::MaskSoftClipped {});
            }
        }
        
        if (!options.at("disable-adapter-masking").as<bool>()) {
            result.register_transform(ReadTransforms::MaskAdapters {});
        }
        
        if (!options.at("disable-overlap-masking").as<bool>()) {
            result.register_transform(ReadTransforms::MaskOverlappedSegment {});
        }
        
        result.shrink_to_fit();
        
        return result;
    }
    
    CandidateGeneratorBuilder make_candidate_generator_builder(const po::variables_map& options,
                                                               const ReferenceGenome& reference)
    {
        Logging::WarningLogger warning_log {};
        Logging::ErrorLogger log {};
        
        CandidateGeneratorBuilder result {};
        
        result.set_reference(reference);
        
        if (options.count("candidates-from-source") == 1) {
            result.add_generator(CandidateGeneratorBuilder::Generator::External);
            
            const fs::path input_path {options.at("candidates-from-source").as<std::string>()};
            
            auto resolved_path = resolve_path(input_path, options);
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--candidates-from-source) does not exist";
            }
            
            result.set_variant_source(std::move(resolved_path));
        }
        
        if (options.count("regenotype") == 1) {
            fs::path regenotype_path {options.at("regenotype").as<std::string>()};
            
            if (options.count("candidates-from-source") == 1) {
                fs::path input_path {options.at("candidates-from-source").as<std::string>()};
                
                if (regenotype_path != input_path) {
                    warning_log << "Running in regenotype mode but given a different source variant file";
                } else {
                    return result;
                }
            } else {
                result.add_generator(CandidateGeneratorBuilder::Generator::External);
            }
            
            auto resolved_path = resolve_path(regenotype_path, options);
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << regenotype_path
                            << " given in the input option (--candidates-from-source) does not exist";
            }
            
            result.set_variant_source(std::move(resolved_path));
        }
        
        result.set_min_base_quality(options.at("min-base-quality").as<unsigned>());
        result.set_max_variant_size(options.at("max-variant-size").as<CandidateGeneratorBuilder::SizeType>());
        
        if (options.count("min-supporting-reads")) {
            auto min_supporting_reads = options.at("min-supporting-reads").as<unsigned>();
            
            if (min_supporting_reads == 0) {
                warning_log << "The option --min_supporting_reads was set to 0 - assuming this is a typo and setting to 1";
                ++min_supporting_reads;
            }
            
            result.set_min_supporting_reads(min_supporting_reads);
        } else {
            result.set_min_supporting_reads(2); // TODO: Octopus should automatically calculate this
        }
        
        if (!options.at("no-raw-cigar-candidates").as<bool>()) {
            result.add_generator(CandidateGeneratorBuilder::Generator::Alignment);
        }
        
        if (!options.at("no-assembly-candidates").as<bool>()) {
            result.add_generator(CandidateGeneratorBuilder::Generator::Assembler);
            const auto kmer_sizes = options.at("kmer-size").as<std::vector<unsigned>>();
            for (const auto k : kmer_sizes) {
                result.add_kmer_size(k);
            }
            result.set_assembler_min_base_quality(options.at("min-assembler-base-quality").as<unsigned>());
        }
        
        return result;
    }
    
    void print_ambiguous_contig_ploidies(const std::vector<ContigPloidy>& contig_ploidies,
                                         const po::variables_map& options)
    {
        Logging::WarningLogger log {};
        
        log << "Ambiguous ploidies found";
        
        for (auto it = std::cbegin(contig_ploidies), end = std::cend(contig_ploidies); it != end;) {
            it = std::adjacent_find(it, std::cend(contig_ploidies),
                                    [] (const auto& lhs, const auto& rhs) {
                                        return lhs.contig == rhs.contig;
                                    });
            if (it != std::cend(contig_ploidies)) {
                const auto it2 = std::find_if(std::next(it), std::cend(contig_ploidies),
                                              [=] (const auto& cp) {
                                                  return it->contig != cp.contig;
                                              });
                
                std::ostringstream ss {};
                
                std::copy(it, it2, std::ostream_iterator<ContigPloidy>(ss, " "));
                
                log << ss.str();
                
                it = it2;
            }
        }
    }
    
    void remove_duplicate_ploidies(std::vector<ContigPloidy>& contig_ploidies)
    {
        std::sort(std::begin(contig_ploidies), std::end(contig_ploidies),
                  [] (const auto& lhs, const auto& rhs) {
                      return (lhs.contig == rhs.contig) ? lhs.ploidy < rhs.ploidy : lhs.contig < rhs.contig;
                  });
        
        const auto it = std::unique(std::begin(contig_ploidies), std::end(contig_ploidies),
                                    [] (const auto& lhs, const auto& rhs) {
                                        return lhs.contig == rhs.contig && lhs.ploidy == rhs.ploidy;
                                    });
        
        contig_ploidies.erase(it, std::end(contig_ploidies));
    }
    
    bool has_ambiguous_ploidies(const std::vector<ContigPloidy>& contig_ploidies)
    {
        const auto it2 = std::adjacent_find(std::cbegin(contig_ploidies), std::cend(contig_ploidies),
                                            [] (const auto& lhs, const auto& rhs) {
                                                return lhs.contig == rhs.contig;
                                            });
        return it2 != std::cend(contig_ploidies);
    }
    
    boost::optional<std::vector<ContigPloidy>> extract_contig_ploidies(const po::variables_map& options)
    {
        std::vector<ContigPloidy> result {};
        
        if (options.count("contig-ploidies-file") == 1) {
            const fs::path input_path {options.at("contig-ploidies-file").as<std::string>()};
            
            const auto resolved_path = resolve_path(input_path, options);
            
            Logging::ErrorLogger log {};
            
            if (!fs::exists(resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--contig-ploidies-file) does not exist";
                return boost::none;
            }
            
            std::ifstream file {resolved_path.string()};
            
            std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                           std::back_inserter(result), [] (const Line& line) {
                               std::istringstream ss {line.line_data};
                               ContigPloidy result {};
                               ss >> result;
                               return result;
                           });
        }
        
        if (options.count("contig-ploidies") == 1) {
            append(options.at("contig-ploidies").as<std::vector<ContigPloidy>>(), result);
        }
        
        remove_duplicate_ploidies(result);
        
        if (has_ambiguous_ploidies(result)) {
            print_ambiguous_contig_ploidies(result, options);
            return boost::none;
        }
        
        return result;
    }
    
    bool call_sites_only(const po::variables_map& options)
    {
        return options.at("sites-only").as<bool>();
    }
    
    auto make_haplotype_generator_builder(const po::variables_map& options)
    {
        using LaggingPolicy = HaplotypeGenerator::Builder::Policies::Lagging;
        
        LaggingPolicy lagging_policy;
        switch (options.at("phasing-level").as<PhasingLevel>()) {
            case PhasingLevel::Minimal:
                lagging_policy = LaggingPolicy::None;
                break;
            case PhasingLevel::Conservative:
                lagging_policy = LaggingPolicy::Conservative;
                break;
            case PhasingLevel::Aggressive:
                lagging_policy = LaggingPolicy::Aggressive;
                break;
        }
        
        const auto max_haplotypes = options.at("max-haplotypes").as<unsigned>();
        
        return HaplotypeGenerator::Builder()
            .set_target_limit(max_haplotypes).set_holdout_limit(2048).set_overflow_limit(16384)
            .set_lagging_policy(lagging_policy).set_max_holdout_depth(3);
    }
    
    VariantCallerFactory
    make_variant_caller_factory(const ReferenceGenome& reference,
                                ReadPipe& read_pipe,
                                const CandidateGeneratorBuilder& candidate_generator_builder,
                                const InputRegionMap& regions,
                                const po::variables_map& options)
    {
        VariantCallerBuilder vc_builder {
            reference, read_pipe, candidate_generator_builder,
            make_haplotype_generator_builder(options)
        };
        
        auto caller = options.at("caller").as<std::string>();
        
        if (caller == "population" && read_pipe.num_samples() == 1) {
            caller = "individual";
        }
        
        vc_builder.set_caller(caller);
        
        if (options.count("report-refcalls") == 1) {
            const auto refcall_type = options.at("report-refcalls").as<RefCallType>();
            
            if (refcall_type == RefCallType::Positional) {
                vc_builder.set_refcall_type(VariantCallerBuilder::RefCallType::Positional);
            } else {
                vc_builder.set_refcall_type(VariantCallerBuilder::RefCallType::Blocked);
            }
        } else {
            vc_builder.set_refcall_type(VariantCallerBuilder::RefCallType::None);
        }
        
        auto min_variant_posterior = options.at("min-variant-posterior").as<Phred<double>>();
        
        if (options.count("regenotype") == 1) {
            if (caller == "cancer") {
                vc_builder.set_min_variant_posterior(min_variant_posterior);
            } else {
                vc_builder.set_min_variant_posterior(Phred<double> {1});
            }
        } else {
            vc_builder.set_min_variant_posterior(min_variant_posterior);
        }
        
        auto min_refcall_posterior = options.at("min-refcall-posterior").as<Phred<double>>();
        
        vc_builder.set_min_refcall_posterior(min_refcall_posterior);
        vc_builder.set_max_haplotypes(options.at("max-haplotypes").as<unsigned>());
        vc_builder.set_min_haplotype_posterior(options.at("min-haplotype-posterior").as<float>());
        
        auto min_phase_score = options.at("min-phase-score").as<Phred<double>>();
        vc_builder.set_min_phase_score(min_phase_score);
        
        vc_builder.set_snp_heterozygosity(options.at("snp-heterozygosity").as<float>());
        vc_builder.set_indel_heterozygosity(options.at("indel-heterozygosity").as<float>());
        
        if (caller == "cancer") {
            if (options.count("normal-sample") == 1) {
                auto normal_sample = options.at("normal-sample").as<std::string>();
                
                const auto& samples = read_pipe.samples();
                
                if (std::find(std::cbegin(samples), std::cend(samples),
                              normal_sample) == std::cend(samples)) {
                    Logging::WarningLogger log {};
                    stream(log) << "The given normal sample \"" << normal_sample
                                << "\" was not found in the read files";
                } else {
                    vc_builder.set_normal_sample(std::move(normal_sample));
                }
            } else {
                Logging::WarningLogger log {};
                stream(log) << "No normal sample was given so assuming all samples are tumour";
            }
            
            vc_builder.set_somatic_mutation_rate(options.at("somatic-mutation-rate").as<float>());
            vc_builder.set_min_somatic_frequency(options.at("min-somatic-frequency").as<float>());
            vc_builder.set_credible_mass(options.at("credible-mass").as<float>());
            
            Phred<double> min_somatic_posterior {options.at("min-somatic-posterior").as<double>()};
            
            vc_builder.set_min_somatic_posterior(min_somatic_posterior);
        } else if (caller == "trio") {
            vc_builder.set_maternal_sample(options.at("maternal-sample").as<std::string>());
            vc_builder.set_paternal_sample(options.at("paternal-sample").as<std::string>());
        }
        
        vc_builder.set_model_filtering(!(options.at("disable-call-filtering").as<bool>()
                                       || options.at("disable-model-filtering").as<bool>()));
        
        const auto contig_ploidies = extract_contig_ploidies(options);
        
        if (!contig_ploidies) {
            // TODO
        }
        
        if (call_sites_only(options)) {
            vc_builder.set_sites_only();
        }
        
        vc_builder.set_flank_scoring(!options.at("disable-inactive-flank-scoring").as<bool>());
        
        VariantCallerFactory result {std::move(vc_builder), options.at("organism-ploidy").as<unsigned>()};
        
        for (const auto& p : regions) {
            const auto it = std::find_if(std::cbegin(*contig_ploidies), std::cend(*contig_ploidies),
                                         [&] (const auto& cp) { return cp.contig == p.first; });
            if (it != std::cend(*contig_ploidies)) {
                result.set_contig_ploidy(p.first, it->ploidy);
            }
        }
        
        return result;
    }
    
    boost::optional<fs::path> get_final_output_path(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        const auto input_path = options.at("output").as<std::string>();
        
        if (input_path == "-") {
            return fs::path {input_path}; // Output goes to stdout
        }
        
        const auto resolved_path = resolve_path(input_path, options);
        
        if (!is_file_writable(resolved_path)) {
            stream(log) << "The path " << input_path << " given in the input option output is not writable";
            return boost::none;
        }
        
        return resolved_path;
    }
    
    VcfWriter make_output_vcf_writer(const po::variables_map& options)
    {
        auto out_path = get_final_output_path(options);
        
        if (out_path) {
            return VcfWriter {*std::move(out_path)};
        }
        
        return VcfWriter {};
    }
    
    boost::optional<fs::path> create_temp_file_directory(const po::variables_map& options)
    {
        const auto working_directory = get_working_directory(options);
        
        auto result = working_directory;
        
        const fs::path temp_dir_base_name {"octopus-temp"};
        
        result /= temp_dir_base_name;
        
        constexpr unsigned temp_dir_name_count_limit {10000};
        
        unsigned temp_dir_counter {2};
        
        Logging::WarningLogger log {};
        
        while (fs::exists(result) && temp_dir_counter <= temp_dir_name_count_limit) {
            if (fs::is_empty(result)) {
                stream(log) << "Found empty temporary directory " << result
                            << ", it may need to be deleted manually";
            }
            result = working_directory;
            result /= temp_dir_base_name.string() + "-" + std::to_string(temp_dir_counter);
            ++temp_dir_counter;
        }
        
        if (temp_dir_counter > temp_dir_name_count_limit) {
            log << "There are many temporary directories in working directory indicating an error"
            " - new directory request blocked";
            return boost::none;
        }
        
        if (!fs::create_directory(result)) {
            stream(log) << "Failed to create temporary directory " << result << " - check permissions";
            return boost::none;
        }
        
        return result;
        
        return boost::none;
    }
    } // namespace Options
} // namespace Octopus
