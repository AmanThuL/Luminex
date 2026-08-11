//----------------------------------------------------------------------------------------------------------------------
/// @file NoApi.h
/// @brief Includes the whole address-first prototype interface.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/BindlessTable.h"
#include "NoApi/CommandBuffer.h"
#include "NoApi/DepthStencilState.h"
#include "NoApi/Device.h"
#include "NoApi/FrameRing.h"
#include "NoApi/Handles.h"
#include "NoApi/LinearAllocator.h"
#include "NoApi/Memory.h"
#include "NoApi/Pipeline.h"
#include "NoApi/RenderPass.h"
#include "NoApi/ResidencySet.h"
#include "NoApi/Result.h"
#include "NoApi/Sampler.h"
#include "NoApi/Semaphore.h"
#include "NoApi/Texture.h"
#include "NoApi/Types.h"

/// Address-first GPU execution interface distilled from the "No Graphics API" model.
///
/// This namespace is a frozen, non-normative research artifact. It is not the production RHI, it
/// accepts no feature growth, and nothing outside `Experiments/NoApi/` may depend on it.
namespace lmx::noapi {}
