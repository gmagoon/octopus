//
//  mock_options.h
//  Octopus
//
//  Created by Daniel Cooke on 14/09/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef Octopus_mock_options_h
#define Octopus_mock_options_h

#include <boost/program_options.hpp>
#include <boost/optional.hpp>

#include "program_options.hpp"
#include "test_common.hpp"

namespace po = boost::program_options;

inline boost::optional<po::variables_map> get_basic_mock_options()
{
    const char *argv[] = {
        "octopus",
        
        //"--help",
        //"--version",
        
        "--debug",
        //"--trace",
        
        //"--sites-only",
        
        //"--samples", "NOT-A-SAMPLE",
        
        "--working-directory", "~/Genomics/octopus_test",
        //"--working-directory", "~/Genomics/MCG",
        //"--working-directory", "~/Genomics/cancer/TCGA/benchmark",
        
        //"--target-read-buffer-size", "1.0",
        //"--reference-cache-size", "100",
        //"--threads", "0",
        
        //"--contig-output-order", "AsInReferenceReversed",
        
        "--reference", human_reference_fasta.c_str(),
        //"--reference", ecoli_reference_fasta.c_str(),
        
        //"--reads", NA12878_low_coverage.c_str(),
        "--reads", NA12878_high_coverage.c_str(),
        //"--reads", "~/Genomics/Illumina/NA12878.mapped.ILLUMINA.bwa.CEU.high_coverage_pcr_free.20130906.chr22.bam",
        
        //"--reads", NA12891_high_coverage.c_str(),
        //"--reads", HG00101.c_str(),
        
        //"--reads", NA12878_low_coverage.c_str(), HG00101.c_str(), HG00102.c_str(), HG00103.c_str(),
        //"--reads", NA12878_high_coverage.c_str(), NA12878_simulated_cancer_basic.c_str(), //cancer test
        //"--reads", NA12891_high_coverage.c_str(), NA12878_simulated_cancer_basic.c_str(), //cancer test
        //"--reads", NA12878_high_coverage.c_str(), NA12891_high_coverage.c_str(),
        
        //"--reads", ecoli_bam.c_str(),
        
        // TCGA
        //"--reads", "~/Genomics/cancer/TCGA/benchmark/G15511.HCC1143_BL.1.chr22.bam",
        //"--reads", "~/Genomics/cancer/TCGA/benchmark/G15511.HCC1143.1.chr22.bam",
        
        // MCG
        //"--reads", "~/Genomics/MCG/10120_chr2_47641558_GTA_G.RG.bam",
        //"--reads", "~/Genomics/MCG/D59597_Cov3.RG.bam",
        
        //"--caller", "cancer", // default "population"
        //"--normal-sample", "HCC1143 BL",
        
        //"--organism-ploidy", "3",
        "--contig-ploidies", "MT=1", "Y=1",// "MT=2",
        //"--contig-ploidies-file", contig_ploidies_txt_file.c_str(),
        
        //"--make-blocked-refcalls",
        //"--make-positional-refcalls",
        
        /* input regions */
        
        //"--use-one-based-indexing",
        
        //"--regions", "22:36,922,640-36,922,687", // FP ins?
        //"--regions", "22:35,834,436-35,834,740", // difficult region
        //"--regions", "22:35,909,479-35,909,519", // FP SNV
        //"--regions", "22:36,120,404-36,120,444", // FP SNV
        "--regions", "22:36,468,698-36,468,773", // FP SNV
        
        // MSG
        //"--regions", "2:47,640,494-47,644,304", // whole region
        //"--regions", "2:47,643,156-47,643,222", // true snp
        //"--regions", "2:47,640,572-47,642,302", // first block (no variants)
        //"--regions", "2:47,641,997-47,644,890", // second block (one SNP)
        
        // for population
        
        // NA12878_high_coverage interesting regions / possible errors
        //"--regions", "22:37,894,887-37,895,035", // is the insertion there (both GATK & Platypus call - I'm not so sure)?
        //"--regions", "22:37,616,864-37,617,015", // false negative insertion?
        //"--regions", "22:37,622,869-37,622,909", // SNPs or indels?
        //"--regions", "22:37,622,803-37,623,840", // very repetitive region
        //"--regions", "22:34,974,205-34,975,215", // Another very repetitive region
        //"--regions", "22:35,834,230-35,835,616", // Another very repetitive region
        //"--regions", "22:37,777,920-37,778,151", // is the deletion homozygous?
        //"--regions", "22:38,092,378-38,092,418", // not left aligning the insertion at 38092390
        //"--regions", "22:39,517,648-39,517,952", // is the deletion & insetion homozygous?
        //"--regions", "22:40,946,793-40,946,944", // where is the deletion?
        //"--regions", "22:41,509,085-41,509,236", // false negative insertion (GATK calls, Platypus doesn't)?
        //"--regions", "22:41,988,173-41,988,324", // GATK is calling a deletion at 41988249...
        //"--regions", "22:31,846,671-31,847,001", // interesting deletions
        //"--regions", "22:37,268,614-37,268,778", // sc filter removing true haplotype
        //"--regions", "22:16,103,653-16,103,957", // 5 haplotypes??
        //"--regions", "22:32,033,026-32,033,280", // interesting deletions
        //"--regions", "16:62,432,702-62,432,933", // interesting insertions
        //"--regions", "22:47,397,181-47,397,224", // interesting deletions
        
        // Tricky VCF representation
        //"--regions", "22:19,091,876-19,091,927", // whoa
        //"--regions", "22:16,190,278-16,190,318",  // overlapping deletions
        //"--regions", "22:51004206-51004246", // overlapping deletions
        //"--regions", "22:47,857,586-47,857,737", // snps overlapping deletion
        //"--regions", "22:47,134,127-47,134,167", // SNP followed by insertion
        //"--regions", "22:49929368-49929408", // deletion overlapping SNP
        //"--regions", "22:50957441-50957481", // insertion & SNP, and SNP!
        //"--regions", "22:33,920,251-33,920,291", // insertion followed by SNP
        //"--regions", "22:16,909,216-16,909,255", // insertion followed by 2 SNPs
        //"--regions", "22:25,808,709-25,808,749", // insertions followed by hom SNP
        //"--regions", "22:37,980,669-37,980,708", // overlapping deletion & insertion
        
        //"--regions", "16:46,392,879-46,393,098", // NA12878_low_coverage huge memory spike
        //"--regions", "22:20656122-20656146", // NA12878_high_coverage causing memory spike
        //"--regions", "1:224,024,837-224,024,877", // NA12878_low_coverage edge case insertions
        //"--regions", "1:874950-874951", // NA12878_low_coverage edge case insertions
        //"--regions", "22:16,232,038-16,232,117", // NA12878_high_coverage VERY ineresting deletions!!
        //"--regions", "Y:13451184-13451197", // crazy Y haplotypes NA12878
        //"--regions", "22:37,276,961-37,277,045", // interesting dual deletions NA12878
        //"--regions", "22:23,474,658-23,475,205", // complex insertion/SNP NA12878
        //"--regions", "22:16,231,876-16,232,206", // Complex phasing region
        //"--regions", "22:16,139,705-16,140,266", // NA12878_high_coverage complex region
        //"-L", "6:31,235,688-31,235,883", // TEST phaser
        //"-L", "5:2,726,554-2,726,594", // snp just before deletion
        //"-L", "5:92,593,056", // SNP next to another SNP
        //"-L", "5:92,593,057", // SNP next to another SNP
        //"-L", "5:64,525,420-64,525,459", // two close SNPS
        
        //"--regions", "5:64,525,965-64,526,830", // NA12878_high_coverage crazy alignments
        
        //"--regions", "6:93,705,800-93,706,166", // NA12878_low_coverage no phase
        //"--regions", "6:58,605,652-58,606,061",   // NA12878_low_coverage partial phase
        //"--regions", "6:58,605,687-58,605,779",   // NA12878_low_coverage phase strong
        //"--regions", "3:108,803,741-108,803,854", // NA12878_low_coverage phase weak
        //"--regions", "6:22,877,929-22,878,012", // NA12878_low_coverage HMM error
        //"--regions", "6:89,236,310-89,237,082", // NA12878_high_coverage very nice phasing test
        //"--regions", "21:11,063,185-11,063,327", // weird haplotpes
        //"--regions", "3:47,251,793-47,251,839", // NA12878_high_coverage interesting indels
        //"--regions", "Y:13447283-13447483", // NA12878_low_coverage too many haplotypes
        
        // for cancer
        
        // TCGA HCC1143
        
        //"--regions", "22:29,606,605-29,606,972", // Bad model filter!
        
        //"--regions", "22:18,444,570-18,444,787", // real somatic SNV?
        //"--regions", "22:25,340,394-25,340,434", // real somatic SNV?
        //"--regions", "22:28,681,165-28,681,395", // real somatic SNV?
        //"--regions", "22:27,707,987-27,708,223", // FN low frequency somatic deletion?
        //"--regions", "22:37,166,974-37,167,210", // FN real somatic deletion?
        //"--regions", "22:28,553,936-28,554,100", // somatic insertion? Low posterior
        //"--regions", "22:28,645,596-28,645,926", // Real somatic SNV? Low posterior
        //"--regions", "22:28,970,260-28,970,424", // real somatic deletion?
        //"--regions", "22:29,474,547-29,474,587", // real somatic SNV?
        //"--regions", "22:29,761,141-29,761,305", // real somatic SNV?
        
        //"--regions", "22:30,213,959-30,214,123", // FP somatic SNV. Very difficult.
        //"--regions", "22:29,455,729-29,455,769", // FP somatic deletion
        //"--regions", "22:28,812,452-28,812,616", // FP somatic deletion
        //"--regions", "22:33,595,824-33,595,864", // FP somatic del
        //"--regions", "22:25,731,146-25,731,476", // FP SNP. What's going on here?
        //"--regions", "22:25,656,657-25,656,697", // FP SNP. What's going on here?
        //"--regions", "22:28,063,906-28,064,070", // What's going on here?
        
        //"--regions", "22:27,297,910-27,297,950", // Not a FP, somaticSniper is calling, but what is going on?
        //"--regions", "22:23,842,194-23,842,524", // FP SNV. Solved: Disable overlap masking. Why?
        //"--regions", "22:23,869,889-23,870,053", // FP del. Solved: Disable marked dups filter
        //"--regions", "22:24,160,300-24,160,359", // FP del. Solved: Disable soft clipping
        //"--regions", "22:25,055,737-25,055,901", // FP del. Solved: MQ Filter
        //"--regions", "22:25,656,651-25,656,728", // FP SNP. Solved: Disable MQ filter or allow lagging
        //"--regions", "22:25,330,405-25,330,569", // FP DEL. Solved: model filter
        //"--regions", "22:25,070,597-25,070,854", // FP SNP. Solved: model filter
        
        // For ecoli
        //"--regions", "R00000042:3008660-3020000",
        
        //"--regions-file", hla_regions.c_str(),
        
        //"--skip-regions", "1:1,000,000-2,000,000", "1:1,500,000-10,000,000",
        //"--skip-regions-file", human_skip_regions.c_str(),
        
        // read transforms
        //"--disable-read-transforms",
        //"--disable-soft-clip-masking",
        //"--tail-trim-size", "3",
        //"--disable-adapter-masking",
        //"--disable-overlap-masking",
        
        // read filters
        //"--disable-read-filtering",
        //"--consider-unmapped-reads",
        //"--consider-reads-with-unmapped-segments",
        //"--min-mapping-quality", "1",
        //"--good-base-quality", "0",
        //"--min-good-bases", "0",
        //"--allow-marked-duplicates",
        //"--allow-octopus-duplicates",
        //"--allow-qc-fails",
        
        // downsampling
        //"--disable-downsampling",
        //"--downsample-above", "300",
        //"--downsample-target", "200",
        
        // candidate generation
        //"--min-supporting-reads", "1",
        //"--min-base-quality", "25",
        //"--no-raw-cigar-candidates",
        //"--kmer-size", "5",
        //"--regenotype", "~/Genomics/octopus_test/AllVariants.vcf",
        //"--regenotype", "~/Genomics/cancer/TCGA/benchmark/somatic_sniper_chr22_Q40.vcf",
        //"--candidates-from-source", sample_vcf.c_str(),
        "--no-assembly-candidates",
        //"--kmer-size", "45",
        //"--min-assembler-base-quality", "10",
        //"--max-variant-size", "25",
        
        //"--phasing-level", "Aggressive",
        //"--disable-inactive-flank-scoring",
        //"--max-haplotypes", "64",
        //"--min-haplotype-posterior", "1e-15",
        
        //"--min-variant-posterior", "2",
        //"--min-refcall-posterior", "0",
        "--min-somatic-posterior", "2",
        
        //"--somatics-only",
        
        //"--max-open-read-files", "1",
        
        //"--output", test_out_vcf.c_str(),
        "--output", "octopus_hla.vcf",
        //"--output", "octopus_mcg.vcf",
        //"--output", "octopus_NA12878HC_22_unlagged_dummy.vcf",
        //"--output", "octopus_calls2.vcf",
        //"--output", "octopus_cancer_no_lag.vcf",
        
        nullptr
    };
    
    int argc = sizeof(argv) / sizeof(char*) - 1;
    
    return Octopus::Options::parse_options(argc, argv);
}

#endif
