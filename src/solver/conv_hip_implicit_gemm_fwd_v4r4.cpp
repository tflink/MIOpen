/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include <miopen/conv/invokers/impl_gemm.hpp>
#include <miopen/solver.hpp>
#include <miopen/handle.hpp>
#include <miopen/generic_search.hpp>
#include <miopen/solver/implicitgemm_util.hpp>

#include <cstddef>

MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_HIP_FWD_V4R4)

namespace miopen {
namespace solver {

PerformanceImplicitGemmV4R4Fwd::PerformanceImplicitGemmV4R4Fwd(int BlockSize_,
                                                               int GemmMPerBlock_,
                                                               int GemmNPerBlock_,
                                                               int GemmKPerBlock_,
                                                               int GemmMPerThread_,
                                                               int GemmNPerThread_,
                                                               bool use_spare_set_)
    : BlockSize(BlockSize_),
      GemmMPerBlock(GemmMPerBlock_),
      GemmNPerBlock(GemmNPerBlock_),
      GemmKPerBlock(GemmKPerBlock_),
      GemmMPerThread(GemmMPerThread_),
      GemmNPerThread(GemmNPerThread_),
      use_spare_set(use_spare_set_)
{
}

PerformanceImplicitGemmV4R4Fwd::PerformanceImplicitGemmV4R4Fwd(bool spare)
{
    // always search full space, no matter if use_spare_set or not
    BlockSize = 64;

    GemmMPerBlock = 32;
    GemmNPerBlock = 32;
    GemmKPerBlock = 4;

    GemmMPerThread = 2;
    GemmNPerThread = 2;

    use_spare_set = spare;
}

bool PerformanceImplicitGemmV4R4Fwd::operator==(const PerformanceImplicitGemmV4R4Fwd& other) const
{
    // clang-format off
    return BlockSize == other.BlockSize
        && GemmMPerBlock == other.GemmMPerBlock
        && GemmNPerBlock == other.GemmNPerBlock
        && GemmKPerBlock == other.GemmKPerBlock
        && GemmMPerThread == other.GemmMPerThread
        && GemmNPerThread == other.GemmNPerThread
        && use_spare_set == other.use_spare_set;
    // clang-format on
}

std::tuple<int, bool>
PerformanceImplicitGemmV4R4Fwd::CalculateGridSize(const ConvolutionContext& ctx) const
{
    int GridSize = 0;

    try
    {
        int gemm_m = 0;
        int gemm_n = 0;

        std::tie(gemm_m, gemm_n, std::ignore) = ConvHipImplicitGemmV4R4Fwd::CalculateGemmSize(ctx);

        if(!(gemm_m % GemmMPerBlock == 0 && gemm_n % GemmNPerBlock == 0))
            MIOPEN_THROW("invalid performance parameter");

        GridSize = (gemm_m / GemmMPerBlock) * (gemm_n / GemmNPerBlock);
    }
    catch(...)
    {
        return std::make_tuple(-1, false);
    }

    return std::make_tuple(GridSize, true);
}

std::tuple<int, int, int, int, bool>
PerformanceImplicitGemmV4R4Fwd::CalculateBlockGemmPerformanceParameters(
    const ConvolutionContext&) const
{
    int GemmMLevel0Cluster = 0;
    int GemmNLevel0Cluster = 0;
    int GemmMLevel1Cluster = 0;
    int GemmNLevel1Cluster = 0;

    try
    {
        if(BlockSize == 64)
        {
            GemmMLevel0Cluster = 4;
            GemmNLevel0Cluster = 4;
            GemmMLevel1Cluster = 2;
            GemmNLevel1Cluster = 2;
        }
        else if(BlockSize == 128)
        {
            GemmMLevel0Cluster = 4;
            GemmNLevel0Cluster = 4;
            GemmMLevel1Cluster = 4;
            GemmNLevel1Cluster = 2;
        }
        else if(BlockSize == 256)
        {
            GemmMLevel0Cluster = 4;
            GemmNLevel0Cluster = 4;
            GemmMLevel1Cluster = 4;
            GemmNLevel1Cluster = 4;
        }
        else
        {
            MIOPEN_LOG_E("BlockSize not supported");
            MIOPEN_THROW("invalid performance parameter");
        }

        if(!(GemmMPerBlock % GemmMPerThread == 0 && GemmNPerBlock % GemmNPerThread == 0))
            MIOPEN_THROW("invalid performance parameter");

        const auto thread_gemm_per_block_m = GemmMPerBlock / GemmMPerThread;
        const auto thread_gemm_per_block_n = GemmNPerBlock / GemmNPerThread;

        const auto thread_gemm_per_cluster_m = GemmMLevel0Cluster * GemmMLevel1Cluster;
        const auto thread_gemm_per_cluster_n = GemmNLevel0Cluster * GemmNLevel1Cluster;

        if(!(thread_gemm_per_block_m % thread_gemm_per_cluster_m == 0) &&
           (thread_gemm_per_block_n % thread_gemm_per_cluster_n == 0))
            MIOPEN_THROW("invalid performance parameter");

        const auto cluster_per_block_m = thread_gemm_per_block_m / thread_gemm_per_cluster_m;
        const auto cluster_per_block_n = thread_gemm_per_block_n / thread_gemm_per_cluster_n;

        // inline asm only support cluster_per_block_m = 2 andcluster_per_block_n = 2
        if(!(cluster_per_block_m == 2 && cluster_per_block_n == 2))
            MIOPEN_THROW("invalid performance parameter");
    }
    catch(...)
    {
        return std::make_tuple(-1, -1, -1, -1, false);
    }

    return std::make_tuple(
        GemmMLevel0Cluster, GemmNLevel0Cluster, GemmMLevel1Cluster, GemmNLevel1Cluster, true);
}

std::tuple<int, int, int, int, bool>
PerformanceImplicitGemmV4R4Fwd::CalculateGemmABlockCopyPerformanceParameters(
    const ConvolutionContext&) const
{
    int ClusterLengths_GemmK  = 0;
    int ClusterLengths_GemmM  = 0;
    int SrcDataPerRead_GemmK  = amd_buffer_load_max_length<float>();
    int DstDataPerWrite_GemmM = amd_lds_write_max_length<float>();

    try
    {
        // calculate vector length on gemmk dimension
        SrcDataPerRead_GemmK = gcd(SrcDataPerRead_GemmK, GemmKPerBlock);

        // calculate threadwise copy size
        const auto a_data_per_thread_copy = (GemmKPerBlock * GemmMPerBlock) / BlockSize;

        if(!(a_data_per_thread_copy > 0))
            MIOPEN_THROW("invalid performance parameter");

        // GemmABlockCopySrcDataPerRead_GemmK also bounded by size of threadwise copy
        SrcDataPerRead_GemmK = gcd(SrcDataPerRead_GemmK, a_data_per_thread_copy);

        // decide threadwise copy lengths
        const auto a_data_per_thread_copy_gemmk = SrcDataPerRead_GemmK;
        const auto a_data_per_thread_copy_gemmm =
            a_data_per_thread_copy / a_data_per_thread_copy_gemmk;

        // GemmABlockCopyDstDataPerWrite_GemmM also bounded by size of threadwise copy
        DstDataPerWrite_GemmM = gcd(DstDataPerWrite_GemmM, a_data_per_thread_copy_gemmm);

        // calculate blockwise copy thread cluster lengths
        ClusterLengths_GemmK = GemmKPerBlock / a_data_per_thread_copy_gemmk;
        ClusterLengths_GemmM = GemmMPerBlock / a_data_per_thread_copy_gemmm;

        if(!(ClusterLengths_GemmK > 0 && ClusterLengths_GemmM > 0))
            MIOPEN_THROW("invalid performance parameter");
    }
    catch(...)
    {
        return std::make_tuple(-1, -1, -1, -1, false);
    }

    return std::make_tuple(ClusterLengths_GemmK,
                           ClusterLengths_GemmM,
                           SrcDataPerRead_GemmK,
                           DstDataPerWrite_GemmM,
                           true);
}

std::tuple<int, int, int, int, bool>
PerformanceImplicitGemmV4R4Fwd::CalculateGemmBBlockCopyPerformanceParameters(
    const ConvolutionContext& ctx) const
{
    int ClusterLengths_GemmK  = 0;
    int ClusterLengths_GemmN  = 0;
    int SrcDataPerRead_GemmN  = amd_buffer_load_max_length<float>();
    int DstDataPerWrite_GemmN = amd_lds_write_max_length<float>();

    try
    {
        SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, GemmNPerBlock);

        // calculate vector length on gemmn dimension
        const auto y             = ProblemInterpreter::GetFilterHeightY(ctx.problem);
        const auto x             = ProblemInterpreter::GetFilterWidthX(ctx.problem);
        const auto hi            = ProblemInterpreter::GetInputHeightHi(ctx.problem);
        const auto wi            = ProblemInterpreter::GetInputWidthWi(ctx.problem);
        const auto conv_stride_h = ProblemInterpreter::GetAdjustedConvolutionStrideH(ctx.problem);
        const auto conv_stride_w = ProblemInterpreter::GetAdjustedConvolutionStrideW(ctx.problem);
        const auto conv_dilation_w =
            ProblemInterpreter::GetAdjustedConvolutionDilationW(ctx.problem);
        const auto in_left_pad_h  = ProblemInterpreter::GetInputLeftPadH(ctx.problem);
        const auto in_left_pad_w  = ProblemInterpreter::GetInputLeftPadW(ctx.problem);
        const auto in_right_pad_h = ProblemInterpreter::GetAdjustedInputRightPadH(ctx.problem);
        const auto in_right_pad_w = ProblemInterpreter::GetAdjustedInputRightPadW(ctx.problem);

        if(ctx.problem.Is3d())
        {
            const auto di = ProblemInterpreter::GetInputDepthDi(ctx.problem);
            const auto z  = ProblemInterpreter::GetFilterDepthZ(ctx.problem);
            const auto conv_stride_d =
                ProblemInterpreter::GetAdjustedConvolutionStrideD(ctx.problem);
            const auto in_left_pad_d  = ProblemInterpreter::GetInputLeftPadD(ctx.problem);
            const auto in_right_pad_d = ProblemInterpreter::GetAdjustedInputRightPadD(ctx.problem);

            if(z == 1 && y == 1 && x == 1 && conv_stride_d == 1 && conv_stride_h == 1 &&
               conv_stride_w == 1 && in_left_pad_d == 0 && in_left_pad_h == 0 &&
               in_left_pad_w == 0 && in_right_pad_d == 0 && in_right_pad_h == 0 &&
               in_right_pad_w == 0)
            {
                // \todo there are more configs that can go through this if branch
                SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, di * hi * wi);
            }
            else if(conv_stride_w == 1)
            {
                SrcDataPerRead_GemmN =
                    gcd(SrcDataPerRead_GemmN, in_left_pad_w, wi, in_right_pad_w, conv_dilation_w);
            }
            else
            {
                SrcDataPerRead_GemmN = 1;
            }
        }
        else
        {
            if(y == 1 && x == 1 && conv_stride_h == 1 && conv_stride_w == 1 && in_left_pad_h == 0 &&
               in_left_pad_w == 0 && in_right_pad_h == 0 && in_right_pad_w == 0)
            {
                // \todo there are more configs that can go through this if branch
                SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, hi * wi);
            }
            else if(conv_stride_w == 1)
            {
                SrcDataPerRead_GemmN =
                    gcd(SrcDataPerRead_GemmN, in_left_pad_w, wi, in_right_pad_w, conv_dilation_w);
            }
            else
            {
                SrcDataPerRead_GemmN = 1;
            }
        }

        // calculate threadwise copy size
        const auto b_data_per_thread_copy = (GemmKPerBlock * GemmNPerBlock) / BlockSize;

        if(!(b_data_per_thread_copy > 0))
            MIOPEN_THROW("invalid performance parameter");

        // GemmBBlockCopySrcDataPerRead_GemmN also bounded by size of threadwise copy
        SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, b_data_per_thread_copy);

        const auto b_data_per_thread_copy_gemmn = SrcDataPerRead_GemmN;
        const auto b_data_per_thread_copy_gemmk =
            b_data_per_thread_copy / b_data_per_thread_copy_gemmn;

        // GemmBBlockCopyDstDataPerWrite_GemmN also bounded by size of threadwise copy
        DstDataPerWrite_GemmN = gcd(DstDataPerWrite_GemmN, b_data_per_thread_copy_gemmn);

        if(b_data_per_thread_copy_gemmk == 0)
            MIOPEN_THROW("DIV/0 with b_data_per_thread_copy_gemmk");
        // calculate blockwise copy thread cluster lengths
        ClusterLengths_GemmK = GemmKPerBlock / b_data_per_thread_copy_gemmk;
        ClusterLengths_GemmN = GemmNPerBlock / b_data_per_thread_copy_gemmn;

        if(!(ClusterLengths_GemmK > 0 && ClusterLengths_GemmN > 0))
            MIOPEN_THROW("invalid performance parameter");
    }
    catch(...)
    {
        MIOPEN_LOG_I("catch");
        return std::make_tuple(-1, -1, -1, -1, false);
    }

    return std::make_tuple(ClusterLengths_GemmK,
                           ClusterLengths_GemmN,
                           SrcDataPerRead_GemmN,
                           DstDataPerWrite_GemmN,
                           true);
}

std::tuple<int, bool> PerformanceImplicitGemmV4R4Fwd::CalculateGemmCThreadCopyPerformanceParameters(
    const ConvolutionContext& ctx) const
{
    int DstDataPerWrite_GemmN1 = amd_buffer_store_max_length<float>();

    try
    {
        // GemmCThreadCopyDstDataPerWrite_GemmN1 bounded by size of threadwise GEMM
        DstDataPerWrite_GemmN1 = gcd(DstDataPerWrite_GemmN1, GemmNPerThread);

        // GemmCThreadCopyDstDataPerWrite_GemmN1 limited by global memory layout of output tensor
        const auto ho = ProblemInterpreter::GetOutputHeightHo(ctx.problem);
        const auto wo = ProblemInterpreter::GetOutputWidthWo(ctx.problem);
        DstDataPerWrite_GemmN1 =
            ctx.problem.Is3d() ? gcd(DstDataPerWrite_GemmN1,
                                     ho * wo * ProblemInterpreter::GetOutputDepthDo(ctx.problem))
                               : gcd(DstDataPerWrite_GemmN1, ho * wo);
    }
    catch(...)
    {
        return std::make_tuple(-1, false);
    }

    return std::make_tuple(DstDataPerWrite_GemmN1, true);
}

std::tuple<std::size_t, bool>
PerformanceImplicitGemmV4R4Fwd::CalculateLdsNumberOfByte(const ConvolutionContext& ctx) const
{
    std::size_t lds_size = 0;

    try
    {
        bool valid = false;

        int GemmABlockCopyDescDataPerWriteGemmM = 0;
        std::tie(
            std::ignore, std::ignore, std::ignore, GemmABlockCopyDescDataPerWriteGemmM, valid) =
            CalculateGemmABlockCopyPerformanceParameters(ctx);

        if(!valid)
            MIOPEN_THROW("invalid performance parameter");

        int GemmBBlockCopyDescDataPerWriteGemmN = 0;
        std::tie(
            std::ignore, std::ignore, std::ignore, GemmBBlockCopyDescDataPerWriteGemmN, valid) =
            CalculateGemmBBlockCopyPerformanceParameters(ctx);

        if(!valid)
            MIOPEN_THROW("invalid performance parameter");

        const auto ThreadGemmDataPerRead_GemmM = GemmMPerThread;
        const auto ThreadGemmDataPerRead_GemmN = GemmNPerThread;

        const auto max_lds_align = lcm(GemmABlockCopyDescDataPerWriteGemmM,
                                       GemmBBlockCopyDescDataPerWriteGemmN,
                                       ThreadGemmDataPerRead_GemmM,
                                       ThreadGemmDataPerRead_GemmN);

        const auto a_block_space =
            GemmKPerBlock * integer_least_multiple(GemmMPerBlock, max_lds_align);
        const auto b_block_space =
            GemmKPerBlock * integer_least_multiple(GemmNPerBlock, max_lds_align);

        lds_size = 2 * (a_block_space + b_block_space) * sizeof(float);
    }
    catch(...)
    {
        return std::make_tuple(0, false);
    }

    return std::make_tuple(lds_size, true);
}

bool PerformanceImplicitGemmV4R4Fwd::IsValidValue() const
{
    // clang-format off
    return IsTwoPower<64, 256>(BlockSize) &&
           IsTwoPower<32, 128>(GemmMPerBlock) &&
           IsTwoPower<32, 128>(GemmNPerBlock) &&
           IsTwoPower<4, 16>(GemmKPerBlock) &&
           IsTwoPower<2, 4>(GemmMPerThread) &&
           IsTwoPower<2, 4>(GemmNPerThread);
    // clang-format on
}

bool PerformanceImplicitGemmV4R4Fwd::IsValid(const ConvolutionContext& ctx) const
{
    if(!IsValidValue())
        return false;

    bool valid = false;

    // check blockwise GEMM size
    int gemm_m = 0;
    int gemm_n = 0;
    int gemm_k = 0;

    std::tie(gemm_m, gemm_n, gemm_k) = ConvHipImplicitGemmV4R4Fwd::CalculateGemmSize(ctx);

    if(!(gemm_m % GemmMPerBlock == 0 && gemm_n % GemmNPerBlock == 0 && gemm_k % GemmKPerBlock == 0))
        return false;

    if(!(GemmMPerBlock % GemmMPerThread == 0 && GemmNPerBlock % GemmNPerThread == 0))
        return false;

    // check thread cluster in blockwise GEMM
    std::tie(std::ignore, std::ignore, std::ignore, std::ignore, valid) =
        CalculateBlockGemmPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check blockwise copy of A matrix
    std::tie(std::ignore, std::ignore, std::ignore, std::ignore, valid) =
        CalculateGemmABlockCopyPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check blockwise copy of B matrix
    std::tie(std::ignore, std::ignore, std::ignore, std::ignore, valid) =
        CalculateGemmBBlockCopyPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check threadwise copy of C matrix
    std::tie(std::ignore, valid) = CalculateGemmCThreadCopyPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check LDS allocation
    std::size_t lds_size      = 0;
    std::tie(lds_size, valid) = CalculateLdsNumberOfByte(ctx);

    return (valid and lds_size <= get_lds_max_number_of_byte());
}

void PerformanceImplicitGemmV4R4Fwd::HeuristicInit(const ConvolutionContext& ctx)
{
    PerformanceImplicitGemmV4R4Fwd config;

    config = {256, 128, 128, 16, 4, 4};
    if(!config.IsValid(ctx))
        config = {256, 128, 128, 8, 4, 4};
    if(!config.IsValid(ctx))
        config = {256, 128, 128, 4, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 128, 64, 16, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 128, 64, 8, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 128, 64, 4, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 64, 128, 16, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 64, 128, 8, 4, 4};
    if(!config.IsValid(ctx))
        config = {128, 64, 128, 4, 4, 4};
    if(!config.IsValid(ctx))
        config = {64, 64, 64, 16, 4, 4};
    if(!config.IsValid(ctx))
        config = {64, 64, 64, 8, 4, 4};
    if(!config.IsValid(ctx))
        config = {64, 64, 64, 4, 4, 4};
    if(!config.IsValid(ctx))
        config = {64, 64, 32, 16, 4, 2};
    if(!config.IsValid(ctx))
        config = {64, 64, 32, 8, 4, 2};
    if(!config.IsValid(ctx))
        config = {64, 64, 32, 4, 4, 2};
    if(!config.IsValid(ctx))
        config = {64, 32, 64, 16, 2, 4};
    if(!config.IsValid(ctx))
        config = {64, 32, 64, 8, 2, 4};
    if(!config.IsValid(ctx))
        config = {64, 32, 64, 4, 2, 4};
    if(!config.IsValid(ctx))
        config = {64, 32, 32, 16, 2, 2};
    if(!config.IsValid(ctx))
        config = {64, 32, 32, 8, 2, 2};
    if(!config.IsValid(ctx))
        config = {64, 32, 32, 4, 2, 2};
    if(!config.IsValid(ctx))
    {
        MIOPEN_LOG_E("All attempts failed: ");
        assert(false);
    }

    *this = config;
    MIOPEN_LOG_I(ToString());
}

bool PerformanceImplicitGemmV4R4Fwd::SetNextValue(const ConvolutionContext& /*ctx*/)
{
    // always search full space, no matter if use_spare_set or not
    do
    {
        if(!NextTwoPower<64, 256>(BlockSize))
            break;
        if(!NextTwoPower<32, 128>(GemmMPerBlock))
            break;
        if(!NextTwoPower<32, 128>(GemmNPerBlock))
            break;
        if(!NextTwoPower<4, 16>(GemmKPerBlock))
            break;
        if(!NextTwoPower<2, 4>(GemmMPerThread))
            break;
        if(!NextTwoPower<2, 4>(GemmNPerThread))
            break;

        return false;
    } while(false);

    return true;
}

std::tuple<int, int, int>
ConvHipImplicitGemmV4R4Fwd::CalculateGemmSize(const ConvolutionContext& ctx)
{
    const auto n  = ProblemInterpreter::GetBatchN(ctx.problem);
    const auto k  = ProblemInterpreter::GetOutputChannelK(ctx.problem);
    const auto c  = ProblemInterpreter::GetInputChannelC(ctx.problem);
    const auto ho = ProblemInterpreter::GetOutputHeightHo(ctx.problem);
    const auto wo = ProblemInterpreter::GetOutputWidthWo(ctx.problem);
    const auto y  = ProblemInterpreter::GetFilterHeightY(ctx.problem);
    const auto x  = ProblemInterpreter::GetFilterWidthX(ctx.problem);

    const auto gemm_m = k;
    const auto gemm_n = ctx.problem.Is3d()
                            ? n * ho * wo * ProblemInterpreter::GetOutputDepthDo(ctx.problem)
                            : n * ho * wo;
    const auto gemm_k = ctx.problem.Is3d()
                            ? c * y * x * ProblemInterpreter::GetFilterDepthZ(ctx.problem)
                            : c * y * x;

    return std::make_tuple(gemm_m, gemm_n, gemm_k);
}

bool ConvHipImplicitGemmV4R4Fwd::IsApplicable(const ConvolutionContext& ctx) const
{
    if(miopen::IsDisabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_HIP_FWD_V4R4{}))
        return false;
    if(ctx.problem.conv_problem.GetConv().attribute.deterministic)
        return false;
    if(!ctx.use_hip_kernels)
        return false;
    if(!ctx.problem.IsLayoutDefault())
        return false;
    if(!IsComposableKernelSupportedHardware(ctx))
        return false;
    if(!ctx.problem.direction.IsForward())
        return false;
    if(!ctx.problem.Is2d() && !ctx.problem.Is3d())
        return false;
    if(!ctx.problem.IsFp32())
        return false;
    if(ctx.problem.group_counts != 1)
        return false;

    int gemm_m = 0;
    int gemm_n = 0;
    int gemm_k = 0;

    std::tie(gemm_m, gemm_n, gemm_k) = CalculateGemmSize(ctx);
    return gemm_m % 32 == 0 && gemm_n % 32 == 0 && gemm_k % 4 == 0;
}

PerformanceImplicitGemmV4R4Fwd
ConvHipImplicitGemmV4R4Fwd::GetDefaultPerformanceConfig(const ConvolutionContext& ctx) const
{
    return GetPerformanceConfigBase<PerformanceImplicitGemmV4R4Fwd>(ctx);
}

bool ConvHipImplicitGemmV4R4Fwd::IsValidPerformanceConfig(
    const ConvolutionContext& ctx, const PerformanceImplicitGemmV4R4Fwd& config) const
{
    MIOPEN_LOG_I("");
    return config.IsValidValue() && config.IsValid(ctx);
}

PerformanceImplicitGemmV4R4Fwd
ConvHipImplicitGemmV4R4Fwd::Search(const ConvolutionContext& ctx,
                                   const AnyInvokeParams& invoke_ctx) const
{
    return GenericSearch(*this, ctx, ctx.problem, invoke_ctx);
}

ConvSolution
ConvHipImplicitGemmV4R4Fwd::GetSolution(const ConvolutionContext& ctx,
                                        const PerformanceImplicitGemmV4R4Fwd& config) const
{

    ConvSolution result;
    KernelInfo construction_parameters;

    assert(config.IsValid(ctx));

    int grid_size = 0;

    std::tie(grid_size, std::ignore) = config.CalculateGridSize(ctx);

    construction_parameters.l_wk.push_back(config.BlockSize);
    construction_parameters.l_wk.push_back(1);
    construction_parameters.l_wk.push_back(1);

    construction_parameters.g_wk.push_back(config.BlockSize * grid_size);
    construction_parameters.g_wk.push_back(1);
    construction_parameters.g_wk.push_back(1);

    if(ctx.problem.Is3d())
    {
        construction_parameters.kernel_file =
            "static_kernel_gridwise_convolution_implicit_gemm_v4r4_ncdhw_kczyx_nkdhw.cpp";

        construction_parameters.kernel_name =
            "gridwise_convolution_implicit_gemm_v4r4_ncdhw_kczyx_nkdhw";
    }
    else
    {
        construction_parameters.kernel_file =
            "static_kernel_gridwise_convolution_implicit_gemm_v4r4_nchw_kcyx_nkhw.cpp";

        construction_parameters.kernel_name =
            "gridwise_convolution_implicit_gemm_v4r4_nchw_kcyx_nkhw";
    }

    int GemmMLevel0Cluster                    = 0;
    int GemmNLevel0Cluster                    = 0;
    int GemmMLevel1Cluster                    = 0;
    int GemmNLevel1Cluster                    = 0;
    int GemmABlockCopyClusterLengths_GemmK    = 0;
    int GemmABlockCopyClusterLengths_GemmM    = 0;
    int GemmABlockCopySrcDataPerRead_GemmK    = 0;
    int GemmABlockCopyDstDataPerWrite_GemmM   = 0;
    int GemmBBlockCopyClusterLengths_GemmK    = 0;
    int GemmBBlockCopyClusterLengths_GemmN    = 0;
    int GemmBBlockCopySrcDataPerRead_GemmN    = 0;
    int GemmBBlockCopyDstDataPerWrite_GemmN   = 0;
    int GemmCThreadCopyDstDataPerWrite_GemmN1 = 0;

    std::tie(GemmMLevel0Cluster,
             GemmNLevel0Cluster,
             GemmMLevel1Cluster,
             GemmNLevel1Cluster,
             std::ignore) = config.CalculateBlockGemmPerformanceParameters(ctx);

    std::tie(GemmABlockCopyClusterLengths_GemmK,
             GemmABlockCopyClusterLengths_GemmM,
             GemmABlockCopySrcDataPerRead_GemmK,
             GemmABlockCopyDstDataPerWrite_GemmM,
             std::ignore) = config.CalculateGemmABlockCopyPerformanceParameters(ctx);

    std::tie(GemmBBlockCopyClusterLengths_GemmK,
             GemmBBlockCopyClusterLengths_GemmN,
             GemmBBlockCopySrcDataPerRead_GemmN,
             GemmBBlockCopyDstDataPerWrite_GemmN,
             std::ignore) = config.CalculateGemmBBlockCopyPerformanceParameters(ctx);

    std::tie(GemmCThreadCopyDstDataPerWrite_GemmN1, std::ignore) =
        config.CalculateGemmCThreadCopyPerformanceParameters(ctx);

    // clang-format off
    construction_parameters.comp_options =
        std::string(" -DCK_PARAM_PROBLEM_N=") + std::to_string(ProblemInterpreter::GetBatchN(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_K=") + std::to_string(ProblemInterpreter::GetOutputChannelK(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_C=") + std::to_string(ProblemInterpreter::GetInputChannelC(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_HI=") + std::to_string(ProblemInterpreter::GetInputHeightHi(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_WI=") + std::to_string(ProblemInterpreter::GetInputWidthWi(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_HO=") + std::to_string(ProblemInterpreter::GetOutputHeightHo(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_WO=") + std::to_string(ProblemInterpreter::GetOutputWidthWo(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_Y=") + std::to_string(ProblemInterpreter::GetFilterHeightY(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_X=") + std::to_string(ProblemInterpreter::GetFilterWidthX(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_STRIDE_H=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionStrideH(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_STRIDE_W=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionStrideW(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DILATION_H=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionDilationH(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DILATION_W=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionDilationW(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_LEFT_PAD_H=") + std::to_string(ProblemInterpreter::GetInputLeftPadH(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_LEFT_PAD_W=") + std::to_string(ProblemInterpreter::GetInputLeftPadW(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_RIGHT_PAD_H=") + std::to_string(ProblemInterpreter::GetAdjustedInputRightPadH(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_RIGHT_PAD_W=") + std::to_string(ProblemInterpreter::GetAdjustedInputRightPadW(ctx.problem)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_FORWARD=") + std::to_string(1) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_BACKWARD_DATA=") + std::to_string(0) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_BACKWARD_WEIGHT=") + std::to_string(0) +
        std::string(" -DCK_PARAM_TUNABLE_BLOCK_SIZE=") + std::to_string(config.BlockSize) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_PER_BLOCK=") + std::to_string(config.GemmMPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_PER_BLOCK=") + std::to_string(config.GemmNPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_K_PER_BLOCK=") + std::to_string(config.GemmKPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_PER_THREAD=") + std::to_string(config.GemmMPerThread) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_PER_THREAD=") + std::to_string(config.GemmNPerThread) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_LEVEL0_CLUSTER=") + std::to_string(GemmMLevel0Cluster) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_LEVEL0_CLUSTER=") + std::to_string(GemmNLevel0Cluster) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_LEVEL1_CLUSTER=") + std::to_string(GemmMLevel1Cluster) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_LEVEL1_CLUSTER=") + std::to_string(GemmNLevel1Cluster) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_A_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_K=") + std::to_string(GemmABlockCopyClusterLengths_GemmK) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_A_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_M=") + std::to_string(GemmABlockCopyClusterLengths_GemmM) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_A_BLOCK_COPY_SRC_DATA_PER_READ_GEMM_K=") + std::to_string(GemmABlockCopySrcDataPerRead_GemmK) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_A_BLOCK_COPY_DST_DATA_PER_WRITE_GEMM_M=") + std::to_string(GemmABlockCopyDstDataPerWrite_GemmM) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_B_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_K=") + std::to_string(GemmBBlockCopyClusterLengths_GemmK) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_B_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_N=") + std::to_string(GemmBBlockCopyClusterLengths_GemmN) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_B_BLOCK_COPY_SRC_DATA_PER_READ_GEMM_N=") + std::to_string(GemmBBlockCopySrcDataPerRead_GemmN) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_B_BLOCK_COPY_DST_DATA_PER_WRITE_GEMM_N=") + std::to_string(GemmBBlockCopyDstDataPerWrite_GemmN) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_C_THREAD_COPY_DST_DATA_PER_WRITE_GEMM_N1=") + std::to_string(GemmCThreadCopyDstDataPerWrite_GemmN1) +
        std::string(" -DCK_PARAM_DEPENDENT_GRID_SIZE=") + std::to_string(grid_size) +
        std::string(" -DCK_THREADWISE_GEMM_USE_AMD_INLINE_ASM=") + (use_amd_inline_asm(ctx, ctx.problem) ? '1' : '0') +
        std::string(" -DCK_USE_AMD_INLINE_ASM=") + (use_amd_inline_asm(ctx, ctx.problem) ? '1' : '0') +
        get_static_ck_common_compiler_flag(ctx) +
        ctx.general_compile_options;

        if (ctx.problem.Is3d()){
            construction_parameters.comp_options +=
                std::string(" -DCK_PARAM_PROBLEM_DI=") + std::to_string(ProblemInterpreter::GetInputDepthDi(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_DO=") + std::to_string(ProblemInterpreter::GetOutputDepthDo(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_Z=") + std::to_string(ProblemInterpreter::GetFilterDepthZ(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_CONV_STRIDE_D=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionStrideD(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_CONV_DILATION_D=") + std::to_string(ProblemInterpreter::GetAdjustedConvolutionDilationD(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_IN_LEFT_PAD_D=") + std::to_string(ProblemInterpreter::GetInputLeftPadD(ctx.problem)) +
                std::string(" -DCK_PARAM_PROBLEM_IN_RIGHT_PAD_D=") + std::to_string(ProblemInterpreter::GetAdjustedInputRightPadD(ctx.problem)) ;
        }

    // clang-format on

    result.invoker_factory = conv::MakeImplGemmDataInvokerFactory(ctx.problem);
    result.construction_params.push_back(construction_parameters);
    return result;
}

} // namespace solver
} // namespace miopen
