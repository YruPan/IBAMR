// Filename: VCINSStaggeredConservativeConvectiveOperator.cpp
// Created on 01 April 2018 by Nishant Nangia and Amneet Bhalla
//
// Copyright (c) 2002-2017, Nishant Nangia and Amneet Bhalla
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of The University of North Carolina nor the names of
//      its contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/////////////////////////////// INCLUDES /////////////////////////////////////

#include <ostream>
#include <stddef.h>
#include <string>
#include <vector>

#include "Box.h"
#include "CartesianPatchGeometry.h"
#include "FaceData.h"
#include "HierarchyDataOpsManager.h"
#include "HierarchySideDataOpsReal.h"
#include "IBAMR_config.h"
#include "Index.h"
#include "IntVector.h"
#include "MultiblockDataTranslator.h"
#include "Patch.h"
#include "PatchHierarchy.h"
#include "PatchLevel.h"
#include "SAMRAIVectorReal.h"
#include "SideData.h"
#include "SideGeometry.h"
#include "SideVariable.h"
#include "Variable.h"
#include "VariableContext.h"
#include "VariableDatabase.h"
#include "boost/array.hpp"
#include "ibamr/ConvectiveOperator.h"
#include "ibamr/StaggeredStokesPhysicalBoundaryHelper.h"
#include "ibamr/VCINSStaggeredConservativeConvectiveOperator.h"
#include "ibamr/ibamr_enums.h"
#include "ibamr/ibamr_utilities.h"
#include "ibamr/namespaces.h" // IWYU pragma: keep
#include "ibtk/HierarchyGhostCellInterpolation.h"
#include "ibtk/PhysicalBoundaryUtilities.h"
#include "tbox/Database.h"
#include "tbox/Pointer.h"
#include "tbox/Timer.h"
#include "tbox/TimerManager.h"
#include "tbox/Utilities.h"

namespace SAMRAI
{
namespace solv
{
template <int DIM>
class RobinBcCoefStrategy;
} // namespace solv
} // namespace SAMRAI

// FORTRAN ROUTINES
#if (NDIM == 2)
#define CONVECT_DERIVATIVE_FC IBAMR_FC_FUNC_(convect_derivative2d, CONVECT_DERIVATIVE2D)
#define VC_UPDATE_DENSITY_FC IBAMR_FC_FUNC_(vc_update_density2d, VC_UPDATE_DENSITY2D)
#define VC_SSP_RK2_UPDATE_DENSITY_FC IBAMR_FC_FUNC_(vc_ssp_rk2_update_density2d, VC_SSP_RK2_UPDATE_DENSITY2D)
#define NAVIER_STOKES_INTERP_COMPS_FC IBAMR_FC_FUNC_(navier_stokes_interp_comps2d, NAVIER_STOKES_INTERP_COMPS2D)
#define VC_NAVIER_STOKES_UPWIND_QUANTITY_FC                                                                            \
    IBAMR_FC_FUNC_(vc_navier_stokes_upwind_quantity2d, VC_NAVIER_STOKES_UPWIND_QUANTITY2D)
#define VC_NAVIER_STOKES_CUI_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_cui_quantity2d, VC_NAVIER_STOKES_CUI_QUANTITY2D)
#define VC_NAVIER_STOKES_FBICS_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_fbics_quantity2d, VC_NAVIER_STOKES_FBICS_QUANTITY2D)
#define VC_NAVIER_STOKES_MGAMMA_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_mgamma_quantity2d, VC_NAVIER_STOKES_MGAMMA_QUANTITY2D)
#define VC_NAVIER_STOKES_COMPUTE_MOMENTUM_FC                                                                       \
    IBAMR_FC_FUNC_(vc_navier_stokes_compute_momentum2d, VC_NAVIER_STOKES_COMPUTE_MOMENTUM2D)
#endif

#if (NDIM == 3)
#define CONVECT_DERIVATIVE_FC IBAMR_FC_FUNC_(convect_derivative3d, CONVECT_DERIVATIVE3D)
#define VC_UPDATE_DENSITY_FC IBAMR_FC_FUNC_(vc_update_density3d, VC_UPDATE_DENSITY3D)
#define VC_SSP_RK2_UPDATE_DENSITY_FC IBAMR_FC_FUNC_(vc_ssp_rk2_update_density3d, VC_SSP_RK2_UPDATE_DENSITY3D)
#define NAVIER_STOKES_INTERP_COMPS_FC IBAMR_FC_FUNC_(navier_stokes_interp_comps3d, NAVIER_STOKES_INTERP_COMPS3D)
#define VC_NAVIER_STOKES_UPWIND_QUANTITY_FC                                                                            \
    IBAMR_FC_FUNC_(vc_navier_stokes_upwind_quantity3d, VC_NAVIER_STOKES_UPWIND_QUANTITY3D)
#define VC_NAVIER_STOKES_CUI_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_cui_quantity3d, VC_NAVIER_STOKES_CUI_QUANTITY3D)
#define VC_NAVIER_STOKES_FBICS_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_fbics_quantity3d, VC_NAVIER_STOKES_FBICS_QUANTITY3D)
#define VC_NAVIER_STOKES_MGAMMA_QUANTITY_FC                                                                               \
    IBAMR_FC_FUNC_(vc_navier_stokes_mgamma_quantity3d, VC_NAVIER_STOKES_MGAMMA_QUANTITY3D)
#define VC_NAVIER_STOKES_COMPUTE_MOMENTUM_FC                                                                       \
    IBAMR_FC_FUNC_(vc_navier_stokes_compute_momentum3d, VC_NAVIER_STOKES_COMPUTE_MOMENTUM3D)
#endif

extern "C" {
void CONVECT_DERIVATIVE_FC(const double*,
#if (NDIM == 2)
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const double*,
                           const double*,
                           const double*,
                           const double*,
                           const int&,
                           const int&,
#endif
#if (NDIM == 3)
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const int&,
                           const double*,
                           const double*,
                           const double*,
                           const double*,
                           const double*,
                           const double*,
                           const int&,
                           const int&,
                           const int&,
#endif
                           double*);
void VC_UPDATE_DENSITY_FC(const double*,
                          const double&,
#if (NDIM == 2)
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const double*,
                          const double*,
                          const int&,
                          const int&,
                          const double*,
                          const double*,
                          const int&,
                          const int&,
                          const double*,
                          const int&,
                          const int&,
#endif
#if (NDIM == 3)
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const int&,
                          const double*,
                          const double*,
                          const double*,
                          const int&,
                          const int&,
                          const int&,
                          const double*,
                          const double*,
                          const double*,
                          const int&,
                          const int&,
                          const int&,
                          const double*,
                          const int&,
                          const int&,
                          const int&,
#endif
                          double*);

void VC_SSP_RK2_UPDATE_DENSITY_FC(const double*,
                                  const double&,
#if (NDIM == 2)
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const double*,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const double*,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const int&,
                                  const int&,
#endif
#if (NDIM == 3)
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const double*,
                                  const double*,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const double*,
                                  const double*,
                                  const int&,
                                  const int&,
                                  const int&,
                                  const double*,
                                  const int&,
                                  const int&,
                                  const int&,
#endif
                                  double*);

void NAVIER_STOKES_INTERP_COMPS_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_UPWIND_QUANTITY_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_CUI_QUANTITY_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_CUI_QUANTITY_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_FBICS_QUANTITY_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_MGAMMA_QUANTITY_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    double*,
    double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*
#endif
    );

void VC_NAVIER_STOKES_COMPUTE_MOMENTUM_FC(
#if (NDIM == 2)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    const int&,
    const int&,
    const double*,
    const double*,
    const int&,
    const int&,
    const double*,
    const double*
#endif
#if (NDIM == 3)
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    const int&,
    double*,
    double*,
    double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*,
    const int&,
    const int&,
    const int&,
    const double*,
    const double*,
    const double*
#endif
    );
}

/////////////////////////////// NAMESPACE ////////////////////////////////////

namespace IBAMR
{
/////////////////////////////// STATIC ///////////////////////////////////////

namespace
{
// NOTE: The number of ghost cells required by the convection scheme depends on the chosen
// convective limiter, which will be set via input file
static const int GUPWINDG = 2;
static const int GCUIG = 3;
static const int GFBICSG = 3;
static const int GMGAMMAG = 3;
static const int NOGHOSTS = 0;

// Timers.
static Timer* t_apply_convective_operator;
static Timer* t_apply;
static Timer* t_initialize_operator_state;
static Timer* t_deallocate_operator_state;
}

/////////////////////////////// PUBLIC ///////////////////////////////////////

VCINSStaggeredConservativeConvectiveOperator::VCINSStaggeredConservativeConvectiveOperator(
    const std::string& object_name,
    Pointer<Database> input_db,
    const ConvectiveDifferencingType difference_form,
    const std::vector<RobinBcCoefStrategy<NDIM>*>& bc_coefs)
    : ConvectiveOperator(object_name, difference_form),
      d_bc_coefs(bc_coefs),
      d_bdry_extrap_type("CONSTANT"),
      d_hierarchy(NULL),
      d_coarsest_ln(-1),
      d_finest_ln(-1),
      d_rho_is_set(false),
      d_dt_is_set(false),
      d_dt(-1.0),
      d_rho_interp_bc_coefs(NDIM, static_cast<RobinBcCoefStrategy<NDIM>*>(NULL)),
      d_U_var(NULL),
      d_U_scratch_idx(-1),
      d_rho_interp_var(NULL),
      d_rho_interp_current_idx(-1),
      d_rho_interp_scratch_idx(-1),
      d_rho_interp_new_idx(-1),
      d_vc_velocity_convective_limiter(VC_UPWIND),
      d_vc_density_convective_limiter(VC_UPWIND),
      d_velocity_limiter_gcw(1),
      d_density_limiter_gcw(1),
      d_vc_density_time_stepping_type(VC_FORWARD_EULER)
{
    if (d_difference_form != CONSERVATIVE)
    {
        TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::VCINSStaggeredConservativeConvectiveOperator():\n"
                   << "  unsupported differencing form: "
                   << enum_to_string<ConvectiveDifferencingType>(d_difference_form)
                   << " \n"
                   << "  valid choices are: CONSERVATIVE\n");
    }

    if (input_db)
    {
        if (input_db->keyExists("bdry_extrap_type")) d_bdry_extrap_type = input_db->getString("bdry_extrap_type");
        if (input_db->keyExists("vc_convective_limiter"))
        { 
            d_vc_velocity_convective_limiter = IBAMR::string_to_enum<VCConvectiveLimiter>(input_db->getString("vc_convective_limiter"));
            d_vc_density_convective_limiter = IBAMR::string_to_enum<VCConvectiveLimiter>(input_db->getString("vc_convective_limiter"));
        }
        if (input_db->keyExists("vc_velocity_convective_limiter"))
        {
            d_vc_velocity_convective_limiter = IBAMR::string_to_enum<VCConvectiveLimiter>(input_db->getString("vc_velocity_convective_limiter"));
        }
        if (input_db->keyExists("vc_density_convective_limiter"))
        {
            d_vc_density_convective_limiter = IBAMR::string_to_enum<VCConvectiveLimiter>(input_db->getString("vc_density_convective_limiter"));
        }

        if (input_db->keyExists("vc_density_time_stepping_type"))
        {
            d_vc_density_time_stepping_type =
                IBAMR::string_to_enum<VCDensityTimeSteppingType>(input_db->getString("vc_density_time_stepping_type"));
        }
    }

    switch (d_vc_velocity_convective_limiter)
    {
    case VC_UPWIND:
        d_velocity_limiter_gcw = GUPWINDG;
        break;
    case VC_CUI:
        d_velocity_limiter_gcw = GCUIG;
        break;
    case VC_FBICS:
        d_velocity_limiter_gcw = GFBICSG;
        break;
    case VC_MGAMMA:
        d_velocity_limiter_gcw = GMGAMMAG;
        break;
    default:
        TBOX_ERROR(d_object_name << "::VCINSStaggeredConservativeConvectiveOperator():\n"
                                 << "  unsupported velocity convective limiter: "
                                 << IBAMR::enum_to_string<VCConvectiveLimiter>(d_vc_velocity_convective_limiter)
                                 << " \n"
                                 << "  valid choices are: VC_UPWIND, VC_CUI, VC_FBICS, VC_MGAMMA\n");
    }

    switch (d_vc_density_convective_limiter)
    {
    case VC_UPWIND:
        d_density_limiter_gcw = GUPWINDG;
        break;
    case VC_CUI:
        d_density_limiter_gcw = GCUIG;
        break;
    case VC_FBICS:
        d_density_limiter_gcw = GFBICSG;
        break;
    case VC_MGAMMA:
        d_density_limiter_gcw = GMGAMMAG;
        break;
    default:
        TBOX_ERROR(d_object_name << "::VCINSStaggeredConservativeConvectiveOperator():\n"
                                 << "  unsupported density convective limiter: "
                                 << IBAMR::enum_to_string<VCConvectiveLimiter>(d_vc_density_convective_limiter)
                                 << " \n"
                                 << "  valid choices are: VC_UPWIND, VC_CUI, VC_FBICS, VC_MGAMMA\n");
    }

    switch (d_vc_density_time_stepping_type)
    {
    case VC_FORWARD_EULER:
        break;
    case VC_SSPRK2:
        break;
    default:
        TBOX_ERROR(d_object_name << "::VCINSStaggeredConservativeConvectiveOperator():\n"
                                 << "  unsupported density time stepping type: "
                                 << IBAMR::enum_to_string<VCDensityTimeSteppingType>(d_vc_density_time_stepping_type)
                                 << " \n"
                                 << "  valid choices are: VC_FORWARD_EULER, VC_SSPRK2\n");
    }

    VariableDatabase<NDIM>* var_db = VariableDatabase<NDIM>::getDatabase();
    Pointer<VariableContext> context = var_db->getContext("VCINSStaggeredConservativeConvectiveOperator::CONTEXT");

    const std::string U_var_name = "VCINSStaggeredConservativeConvectiveOperator::U";
    d_U_var = var_db->getVariable(U_var_name);
    if (d_U_var)
    {
        d_U_scratch_idx = var_db->mapVariableAndContextToIndex(d_U_var, context);
    }
    else
    {
        d_U_var = new SideVariable<NDIM, double>(U_var_name);
        d_U_scratch_idx = var_db->registerVariableAndContext(d_U_var, context, IntVector<NDIM>(d_velocity_limiter_gcw));
    }

#if !defined(NDEBUG)
    TBOX_ASSERT(d_U_scratch_idx >= 0);
#endif

    const std::string rho_interp_name = "VCINSStaggeredConservativeConvectiveOperator::RHO_INTERP";
    d_rho_interp_var = var_db->getVariable(rho_interp_name);
    if (d_rho_interp_var)
    {
        d_rho_interp_scratch_idx =
            var_db->mapVariableAndContextToIndex(d_rho_interp_var, var_db->getContext(rho_interp_name + "::SCRATCH"));
        d_rho_interp_new_idx =
            var_db->mapVariableAndContextToIndex(d_rho_interp_var, var_db->getContext(rho_interp_name + "::NEW"));
    }
    else
    {
        d_rho_interp_var = new SideVariable<NDIM, double>(rho_interp_name);
        d_rho_interp_scratch_idx = var_db->registerVariableAndContext(
            d_rho_interp_var, var_db->getContext(rho_interp_name + "::SCRATCH"), IntVector<NDIM>(d_density_limiter_gcw));
        d_rho_interp_new_idx = var_db->registerVariableAndContext(
            d_rho_interp_var, var_db->getContext(rho_interp_name + "::NEW"), IntVector<NDIM>(NOGHOSTS));
    }
#if !defined(NDEBUG)
    TBOX_ASSERT(d_rho_interp_scratch_idx >= 0);
    TBOX_ASSERT(d_rho_interp_new_idx >= 0);
#endif

    // Setup Timers.
    IBAMR_DO_ONCE(
        t_apply_convective_operator = TimerManager::getManager()->getTimer(
            "IBAMR::VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator()");
        t_apply = TimerManager::getManager()->getTimer("IBAMR::VCINSStaggeredConservativeConvectiveOperator::apply()");
        t_initialize_operator_state = TimerManager::getManager()->getTimer(
            "IBAMR::VCINSStaggeredConservativeConvectiveOperator::initializeOperatorState()");
        t_deallocate_operator_state = TimerManager::getManager()->getTimer(
            "IBAMR::VCINSStaggeredConservativeConvectiveOperator::deallocateOperatorState()"););
    return;
} // VCINSStaggeredConservativeConvectiveOperator

VCINSStaggeredConservativeConvectiveOperator::~VCINSStaggeredConservativeConvectiveOperator()
{
    deallocateOperatorState();
    return;
} // ~VCINSStaggeredConservativeConvectiveOperator

void
VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator(const int U_idx, const int N_idx)
{
    // Get hierarchy operation object
    HierarchyDataOpsManager<NDIM>* hier_ops_manager = HierarchyDataOpsManager<NDIM>::getManager();
    d_hier_sc_data_ops =
        hier_ops_manager->getOperationsDouble(new SideVariable<NDIM, double>("sc_var"), d_hierarchy, true);

    IBAMR_TIMER_START(t_apply_convective_operator);
#if !defined(NDEBUG)
    if (!d_is_initialized)
    {
        TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator():\n"
                   << "  operator must be initialized prior to call to applyConvectiveOperator\n");
    }
    TBOX_ASSERT(U_idx == d_u_idx);

    if (!d_rho_is_set)
    {
        TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator():\n"
                   << "  a side-centered density field must be set via setInterpolatedDensityPatchDataIndex()\n"
                   << "  prior to call to applyConvectiveOperator\n");
    }
    TBOX_ASSERT(d_rho_interp_current_idx >= 0);

    if (!d_dt_is_set)
    {
        TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator():\n"
                   << "  the current time step size must be set via  setTimeStepSize()\n"
                   << "  prior to call to applyConvectiveOperator\n");
    }
    TBOX_ASSERT(d_dt >= 0.0);
#endif

    // Fill ghost cell values for velocity
    static const bool homogeneous_bc = false;
    typedef HierarchyGhostCellInterpolation::InterpolationTransactionComponent InterpolationTransactionComponent;
    std::vector<InterpolationTransactionComponent> transaction_comps(1);
    transaction_comps[0] = InterpolationTransactionComponent(d_U_scratch_idx,
                                                             U_idx,
                                                             "CONSERVATIVE_LINEAR_REFINE",
                                                             false,
                                                             "CONSERVATIVE_COARSEN",
                                                             d_bdry_extrap_type,
                                                             false,
                                                             d_bc_coefs);
    d_hier_bdry_fill->resetTransactionComponents(transaction_comps);
    d_hier_bdry_fill->setHomogeneousBc(homogeneous_bc);
    StaggeredStokesPhysicalBoundaryHelper::setupBcCoefObjects(d_bc_coefs, NULL, d_U_scratch_idx, -1, homogeneous_bc);
    d_hier_bdry_fill->fillData(d_solution_time);
    StaggeredStokesPhysicalBoundaryHelper::resetBcCoefObjects(d_bc_coefs, NULL);
    d_hier_bdry_fill->resetTransactionComponents(d_transaction_comps);

    // Fill ghost cells for density
    InterpolationTransactionComponent rho_transaction(d_rho_interp_scratch_idx,
                                                      d_rho_interp_current_idx,
                                                      "CONSERVATIVE_LINEAR_REFINE",
                                                      false,
                                                      "CONSERVATIVE_COARSEN",
                                                      d_bdry_extrap_type,
                                                      false,
                                                      d_rho_interp_bc_coefs);
    Pointer<HierarchyGhostCellInterpolation> hier_rho_bdry_fill = new HierarchyGhostCellInterpolation();
    hier_rho_bdry_fill->initializeOperatorState(rho_transaction, d_hierarchy);
    hier_rho_bdry_fill->fillData(d_solution_time);

    // Compute the old mass
    const int wgt_sc_idx = d_hier_math_ops->getSideWeightPatchDescriptorIndex();
    const double old_mass = d_hier_sc_data_ops->integral(d_rho_interp_current_idx, wgt_sc_idx);
    pout << "Old mass in the domain = " << old_mass << std::endl;

    // Compute the convective derivative.
    for (int ln = d_coarsest_ln; ln <= d_finest_ln; ++ln)
    {
        Pointer<PatchLevel<NDIM> > level = d_hierarchy->getPatchLevel(ln);
        for (PatchLevel<NDIM>::Iterator p(level); p; p++)
        {
            Pointer<Patch<NDIM> > patch = level->getPatch(p());

            const Pointer<CartesianPatchGeometry<NDIM> > patch_geom = patch->getPatchGeometry();
            const double* const dx = patch_geom->getDx();

            const Box<NDIM>& patch_box = patch->getBox();
            const IntVector<NDIM>& patch_lower = patch_box.lower();
            const IntVector<NDIM>& patch_upper = patch_box.upper();

            Pointer<SideData<NDIM, double> > N_data = patch->getPatchData(N_idx);
            Pointer<SideData<NDIM, double> > U_data = patch->getPatchData(d_U_scratch_idx);
            Pointer<SideData<NDIM, double> > R_data = patch->getPatchData(d_rho_interp_scratch_idx);
            Pointer<SideData<NDIM, double> > R_new_data = patch->getPatchData(d_rho_interp_new_idx);

            // Define variables that live on the "faces" of control volumes centered about side-centered staggered
            // velocity components
            const IntVector<NDIM> ghosts = IntVector<NDIM>(1);
            boost::array<Box<NDIM>, NDIM> side_boxes;
            boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_adv_data;
            boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_half_data;
            boost::array<Pointer<FaceData<NDIM, double> >, NDIM> R_half_data;
            boost::array<Pointer<FaceData<NDIM, double> >, NDIM> P_half_data;
            for (unsigned int axis = 0; axis < NDIM; ++axis)
            {
                side_boxes[axis] = SideGeometry<NDIM>::toSideBox(patch_box, axis);
                U_adv_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                U_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                R_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                P_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
            }
            // Interpolate velocity components onto "faces" using simple averages.
            computeAdvectionVelocity(U_adv_data, U_data, patch_lower, patch_upper, side_boxes);

            // Upwind side-centered densities onto faces.
            interpolateSideQuantity(
                R_half_data, U_adv_data, R_data, patch_lower, patch_upper, side_boxes, d_vc_density_convective_limiter);

            // Compute the convective derivative with this density, if necessary
            if (d_vc_density_time_stepping_type == VC_FORWARD_EULER)
            {
                interpolateSideQuantity(U_half_data,
                                        U_adv_data,
                                        U_data,
                                        patch_lower,
                                        patch_upper,
                                        side_boxes,
                                        d_vc_velocity_convective_limiter);

                computeConvectiveDerivative(N_data, P_half_data, U_adv_data, R_half_data, U_half_data, side_boxes, dx);
            }

            // Compute forward Euler update to the side-centered rho quantity rho^{n+1} = rho^n - dt*div(rho_half*u_adv)
            for (unsigned int axis = 0; axis < NDIM; ++axis)
            {
#if (NDIM == 2)
                VC_UPDATE_DENSITY_FC(dx,
                                     d_dt,
                                     side_boxes[axis].lower(0),
                                     side_boxes[axis].upper(0),
                                     side_boxes[axis].lower(1),
                                     side_boxes[axis].upper(1),
                                     U_adv_data[axis]->getGhostCellWidth()(0),
                                     U_adv_data[axis]->getGhostCellWidth()(1),
                                     U_adv_data[axis]->getPointer(0),
                                     U_adv_data[axis]->getPointer(1),
                                     R_half_data[axis]->getGhostCellWidth()(0),
                                     R_half_data[axis]->getGhostCellWidth()(1),
                                     R_half_data[axis]->getPointer(0),
                                     R_half_data[axis]->getPointer(1),
                                     R_data->getGhostCellWidth()(0),
                                     R_data->getGhostCellWidth()(1),
                                     R_data->getPointer(axis),
                                     R_new_data->getGhostCellWidth()(0),
                                     R_new_data->getGhostCellWidth()(1),
                                     R_new_data->getPointer(axis));
#endif
#if (NDIM == 3)
                VC_UPDATE_DENSITY_FC(dx,
                                     d_dt,
                                     side_boxes[axis].lower(0),
                                     side_boxes[axis].upper(0),
                                     side_boxes[axis].lower(1),
                                     side_boxes[axis].upper(1),
                                     side_boxes[axis].lower(2),
                                     side_boxes[axis].upper(2),
                                     U_adv_data[axis]->getGhostCellWidth()(0),
                                     U_adv_data[axis]->getGhostCellWidth()(1),
                                     U_adv_data[axis]->getGhostCellWidth()(2),
                                     U_adv_data[axis]->getPointer(0),
                                     U_adv_data[axis]->getPointer(1),
                                     U_adv_data[axis]->getPointer(2),
                                     R_half_data[axis]->getGhostCellWidth()(0),
                                     R_half_data[axis]->getGhostCellWidth()(1),
                                     R_half_data[axis]->getGhostCellWidth()(2),
                                     R_half_data[axis]->getPointer(0),
                                     R_half_data[axis]->getPointer(1),
                                     R_half_data[axis]->getPointer(2),
                                     R_data->getGhostCellWidth()(0),
                                     R_data->getGhostCellWidth()(1),
                                     R_data->getGhostCellWidth()(2),
                                     R_data->getPointer(axis),
                                     R_new_data->getGhostCellWidth()(0),
                                     R_new_data->getGhostCellWidth()(1),
                                     R_new_data->getGhostCellWidth()(2),
                                     R_new_data->getPointer(axis));
#endif
            }
        }
    }

    // Compute an additional density update if necessary
    if (d_vc_density_time_stepping_type == VC_SSPRK2)
    {
        // Fill ghost cells of the first update
        InterpolationTransactionComponent update_transaction(d_rho_interp_scratch_idx,
                                                             d_rho_interp_new_idx,
                                                             "CONSERVATIVE_LINEAR_REFINE",
                                                             false,
                                                             "CONSERVATIVE_COARSEN",
                                                             d_bdry_extrap_type,
                                                             false,
                                                             d_rho_interp_bc_coefs);
        Pointer<HierarchyGhostCellInterpolation> hier_update_bdry_fill = new HierarchyGhostCellInterpolation();
        hier_update_bdry_fill->initializeOperatorState(update_transaction, d_hierarchy);
        hier_update_bdry_fill->fillData(d_solution_time + d_dt);

        for (int ln = d_coarsest_ln; ln <= d_finest_ln; ++ln)
        {
            Pointer<PatchLevel<NDIM> > level = d_hierarchy->getPatchLevel(ln);
            for (PatchLevel<NDIM>::Iterator p(level); p; p++)
            {
                Pointer<Patch<NDIM> > patch = level->getPatch(p());

                const Pointer<CartesianPatchGeometry<NDIM> > patch_geom = patch->getPatchGeometry();
                const double* const dx = patch_geom->getDx();

                const Box<NDIM>& patch_box = patch->getBox();
                const IntVector<NDIM>& patch_lower = patch_box.lower();
                const IntVector<NDIM>& patch_upper = patch_box.upper();

                Pointer<SideData<NDIM, double> > N_data = patch->getPatchData(N_idx);
                Pointer<SideData<NDIM, double> > U_data = patch->getPatchData(d_U_scratch_idx);
                Pointer<SideData<NDIM, double> > R_data = patch->getPatchData(d_rho_interp_scratch_idx);
                Pointer<SideData<NDIM, double> > R_old_data = patch->getPatchData(d_rho_interp_current_idx);
                Pointer<SideData<NDIM, double> > R_new_data = patch->getPatchData(d_rho_interp_new_idx);

                // Define variables that live on the "faces" of control volumes centered about side-centered staggered
                // velocity components
                const IntVector<NDIM> ghosts = IntVector<NDIM>(1);
                boost::array<Box<NDIM>, NDIM> side_boxes;
                boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_adv_data;
                boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_half_data;
                boost::array<Pointer<FaceData<NDIM, double> >, NDIM> R_half_data;
                boost::array<Pointer<FaceData<NDIM, double> >, NDIM> P_half_data;
                for (unsigned int axis = 0; axis < NDIM; ++axis)
                {
                    side_boxes[axis] = SideGeometry<NDIM>::toSideBox(patch_box, axis);
                    U_adv_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                    U_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                    R_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                    P_half_data[axis] = new FaceData<NDIM, double>(side_boxes[axis], 1, ghosts);
                }
                // Interpolate velocity components onto "faces" using simple averages.
                computeAdvectionVelocity(U_adv_data, U_data, patch_lower, patch_upper, side_boxes);

                // Upwind side-centered densities onto faces.
                interpolateSideQuantity(R_half_data,
                                        U_adv_data,
                                        R_data,
                                        patch_lower,
                                        patch_upper,
                                        side_boxes,
                                        d_vc_density_convective_limiter);

                // Upwind side-centered velocity onto faces.
                interpolateSideQuantity(U_half_data,
                                        U_adv_data,
                                        U_data,
                                        patch_lower,
                                        patch_upper,
                                        side_boxes,
                                        d_vc_velocity_convective_limiter);

                // Compute the convective derivative
                computeConvectiveDerivative(N_data, P_half_data, U_adv_data, R_half_data, U_half_data, side_boxes, dx);

                // Update rho^{n+1} = 0.5 * rho^n + 0.5 * rho(1) - 0.5*dt*div(rho_half*u_adv)
                for (unsigned int axis = 0; axis < NDIM; ++axis)
                {
#if (NDIM == 2)
                    VC_SSP_RK2_UPDATE_DENSITY_FC(dx,
                                                 d_dt,
                                                 side_boxes[axis].lower(0),
                                                 side_boxes[axis].upper(0),
                                                 side_boxes[axis].lower(1),
                                                 side_boxes[axis].upper(1),
                                                 U_adv_data[axis]->getGhostCellWidth()(0),
                                                 U_adv_data[axis]->getGhostCellWidth()(1),
                                                 U_adv_data[axis]->getPointer(0),
                                                 U_adv_data[axis]->getPointer(1),
                                                 R_half_data[axis]->getGhostCellWidth()(0),
                                                 R_half_data[axis]->getGhostCellWidth()(1),
                                                 R_half_data[axis]->getPointer(0),
                                                 R_half_data[axis]->getPointer(1),
                                                 R_old_data->getGhostCellWidth()(0),
                                                 R_old_data->getGhostCellWidth()(1),
                                                 R_old_data->getPointer(axis),
                                                 R_new_data->getGhostCellWidth()(0),
                                                 R_new_data->getGhostCellWidth()(1),
                                                 R_new_data->getPointer(axis));
#endif
#if (NDIM == 3)
                    VC_SSP_RK2_UPDATE_DENSITY_FC(dx,
                                                 d_dt,
                                                 side_boxes[axis].lower(0),
                                                 side_boxes[axis].upper(0),
                                                 side_boxes[axis].lower(1),
                                                 side_boxes[axis].upper(1),
                                                 side_boxes[axis].lower(2),
                                                 side_boxes[axis].upper(2),
                                                 U_adv_data[axis]->getGhostCellWidth()(0),
                                                 U_adv_data[axis]->getGhostCellWidth()(1),
                                                 U_adv_data[axis]->getGhostCellWidth()(2),
                                                 U_adv_data[axis]->getPointer(0),
                                                 U_adv_data[axis]->getPointer(1),
                                                 U_adv_data[axis]->getPointer(2),
                                                 R_half_data[axis]->getGhostCellWidth()(0),
                                                 R_half_data[axis]->getGhostCellWidth()(1),
                                                 R_half_data[axis]->getGhostCellWidth()(2),
                                                 R_half_data[axis]->getPointer(0),
                                                 R_half_data[axis]->getPointer(1),
                                                 R_half_data[axis]->getPointer(2),
                                                 R_old_data->getGhostCellWidth()(0),
                                                 R_old_data->getGhostCellWidth()(1),
                                                 R_old_data->getGhostCellWidth()(2),
                                                 R_old_data->getPointer(axis),
                                                 R_new_data->getGhostCellWidth()(0),
                                                 R_new_data->getGhostCellWidth()(1),
                                                 R_new_data->getGhostCellWidth()(2),
                                                 R_new_data->getPointer(axis));
#endif
                }
            }
        }
    }

    // Compute the new mass
    const double new_mass = d_hier_sc_data_ops->integral(d_rho_interp_new_idx, wgt_sc_idx);
    pout << "New mass in the domain = " << new_mass << std::endl;
    pout << "Change in mass = " << new_mass - old_mass << std::endl;

    // Reset select options
    d_dt = -1.0;
    d_dt_is_set = false;
    d_rho_interp_current_idx = -1;
    d_rho_is_set = false;

    IBAMR_TIMER_STOP(t_apply_convective_operator);
    return;
} // applyConvectiveOperator

void
VCINSStaggeredConservativeConvectiveOperator::initializeOperatorState(const SAMRAIVectorReal<NDIM, double>& in,
                                                                      const SAMRAIVectorReal<NDIM, double>& out)
{
    IBAMR_TIMER_START(t_initialize_operator_state);

    if (d_is_initialized) deallocateOperatorState();

    // Get the hierarchy configuration.
    d_hierarchy = in.getPatchHierarchy();
    d_coarsest_ln = in.getCoarsestLevelNumber();
    d_finest_ln = in.getFinestLevelNumber();
#if !defined(NDEBUG)
    TBOX_ASSERT(d_hierarchy == out.getPatchHierarchy());
    TBOX_ASSERT(d_coarsest_ln == out.getCoarsestLevelNumber());
    TBOX_ASSERT(d_finest_ln == out.getFinestLevelNumber());
#else
    NULL_USE(out);
#endif

    // Setup the interpolation transaction information.
    typedef HierarchyGhostCellInterpolation::InterpolationTransactionComponent InterpolationTransactionComponent;
    d_transaction_comps.resize(1);
    d_transaction_comps[0] = InterpolationTransactionComponent(d_U_scratch_idx,
                                                               in.getComponentDescriptorIndex(0),
                                                               "CONSERVATIVE_LINEAR_REFINE",
                                                               false,
                                                               "CONSERVATIVE_COARSEN",
                                                               d_bdry_extrap_type,
                                                               false,
                                                               d_bc_coefs);

    // Initialize the interpolation operators.
    d_hier_bdry_fill = new HierarchyGhostCellInterpolation();
    d_hier_bdry_fill->initializeOperatorState(d_transaction_comps, d_hierarchy);

    // Initialize the BC helper.
    d_bc_helper = new StaggeredStokesPhysicalBoundaryHelper();
    d_bc_helper->cacheBcCoefData(d_bc_coefs, d_solution_time, d_hierarchy);

    // Allocate data.
    for (int ln = d_coarsest_ln; ln <= d_finest_ln; ++ln)
    {
        Pointer<PatchLevel<NDIM> > level = d_hierarchy->getPatchLevel(ln);
        if (!level->checkAllocated(d_U_scratch_idx)) level->allocatePatchData(d_U_scratch_idx);
        if (!level->checkAllocated(d_rho_interp_scratch_idx)) level->allocatePatchData(d_rho_interp_scratch_idx);
        if (!level->checkAllocated(d_rho_interp_new_idx)) level->allocatePatchData(d_rho_interp_new_idx);
    }

    if (!d_hier_math_ops_external)
    {
        d_hier_math_ops =
            new HierarchyMathOps(d_object_name + "::HierarchyMathOps", d_hierarchy, d_coarsest_ln, d_finest_ln);
    }
    else
    {
#if !defined(NDEBUG)
        TBOX_ASSERT(d_hier_math_ops);
#endif
    }

    d_is_initialized = true;

    IBAMR_TIMER_STOP(t_initialize_operator_state);
    return;
} // initializeOperatorState

void
VCINSStaggeredConservativeConvectiveOperator::deallocateOperatorState()
{
    if (!d_is_initialized) return;

    IBAMR_TIMER_START(t_deallocate_operator_state);

    // Deallocate the communications operators and BC helpers.
    d_hier_bdry_fill.setNull();
    d_bc_helper.setNull();

    // Deallocate data.
    for (int ln = d_coarsest_ln; ln <= d_finest_ln; ++ln)
    {
        Pointer<PatchLevel<NDIM> > level = d_hierarchy->getPatchLevel(ln);
        if (level->checkAllocated(d_U_scratch_idx)) level->deallocatePatchData(d_U_scratch_idx);
        if (level->checkAllocated(d_rho_interp_scratch_idx)) level->deallocatePatchData(d_rho_interp_scratch_idx);
        if (level->checkAllocated(d_rho_interp_new_idx)) level->deallocatePatchData(d_rho_interp_new_idx);
    }

    // Deallocate hierarchy math operations object.
    if (!d_hier_math_ops_external) d_hier_math_ops.setNull();

    d_is_initialized = false;

    IBAMR_TIMER_STOP(t_deallocate_operator_state);
    return;
} // deallocateOperatorState

void
VCINSStaggeredConservativeConvectiveOperator::setInterpolatedDensityPatchDataIndex(int rho_interp_idx)
{
#if !defined(NDEBUG)
    TBOX_ASSERT(rho_interp_idx >= 0);
#endif
    d_rho_is_set = true;
    d_rho_interp_current_idx = rho_interp_idx;
} // setInterpolatedDensityPatchDataIndex

void
VCINSStaggeredConservativeConvectiveOperator::setTimeStepSize(double dt)
{
#if !defined(NDEBUG)
    TBOX_ASSERT(dt >= 0.0);
#endif
    d_dt_is_set = true;
    d_dt = dt;
} // setTimeStepSize

void
VCINSStaggeredConservativeConvectiveOperator::setInterpolatedDensityBoundaryConditions(
    const std::vector<RobinBcCoefStrategy<NDIM>*>& rho_interp_bc_coefs)
{
#if !defined(NDEBUG)
    TBOX_ASSERT(rho_interp_bc_coefs.size() == NDIM);
#endif
    d_rho_interp_bc_coefs = rho_interp_bc_coefs;
    return;
} // setInterpolatedDensityBoundaryConditions

int
VCINSStaggeredConservativeConvectiveOperator::getUpdatedInterpolatedDensityPatchDataIndex()
{
#if !defined(NDEBUG)
    TBOX_ASSERT(d_rho_interp_new_idx >= 0);
#endif
    return d_rho_interp_new_idx;
} // getUpdatedInterpolatedDensityPatchDataIndex

/////////////////////////////// PROTECTED ////////////////////////////////////

/////////////////////////////// PRIVATE //////////////////////////////////////

void
VCINSStaggeredConservativeConvectiveOperator::computeAdvectionVelocity(
    boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_adv_data,
    const Pointer<SideData<NDIM, double> > U_data,
    const IntVector<NDIM>& patch_lower,
    const IntVector<NDIM>& patch_upper,
    const boost::array<Box<NDIM>, NDIM>& side_boxes)
{
#if (NDIM == 2)
    NAVIER_STOKES_INTERP_COMPS_FC(patch_lower(0),
                                  patch_upper(0),
                                  patch_lower(1),
                                  patch_upper(1),
                                  U_data->getGhostCellWidth()(0),
                                  U_data->getGhostCellWidth()(1),
                                  U_data->getPointer(0),
                                  U_data->getPointer(1),
                                  side_boxes[0].lower(0),
                                  side_boxes[0].upper(0),
                                  side_boxes[0].lower(1),
                                  side_boxes[0].upper(1),
                                  U_adv_data[0]->getGhostCellWidth()(0),
                                  U_adv_data[0]->getGhostCellWidth()(1),
                                  U_adv_data[0]->getPointer(0),
                                  U_adv_data[0]->getPointer(1),
                                  side_boxes[1].lower(0),
                                  side_boxes[1].upper(0),
                                  side_boxes[1].lower(1),
                                  side_boxes[1].upper(1),
                                  U_adv_data[1]->getGhostCellWidth()(0),
                                  U_adv_data[1]->getGhostCellWidth()(1),
                                  U_adv_data[1]->getPointer(0),
                                  U_adv_data[1]->getPointer(1));
#endif
#if (NDIM == 3)
    NAVIER_STOKES_INTERP_COMPS_FC(patch_lower(0),
                                  patch_upper(0),
                                  patch_lower(1),
                                  patch_upper(1),
                                  patch_lower(2),
                                  patch_upper(2),
                                  U_data->getGhostCellWidth()(0),
                                  U_data->getGhostCellWidth()(1),
                                  U_data->getGhostCellWidth()(2),
                                  U_data->getPointer(0),
                                  U_data->getPointer(1),
                                  U_data->getPointer(2),
                                  side_boxes[0].lower(0),
                                  side_boxes[0].upper(0),
                                  side_boxes[0].lower(1),
                                  side_boxes[0].upper(1),
                                  side_boxes[0].lower(2),
                                  side_boxes[0].upper(2),
                                  U_adv_data[0]->getGhostCellWidth()(0),
                                  U_adv_data[0]->getGhostCellWidth()(1),
                                  U_adv_data[0]->getGhostCellWidth()(2),
                                  U_adv_data[0]->getPointer(0),
                                  U_adv_data[0]->getPointer(1),
                                  U_adv_data[0]->getPointer(2),
                                  side_boxes[1].lower(0),
                                  side_boxes[1].upper(0),
                                  side_boxes[1].lower(1),
                                  side_boxes[1].upper(1),
                                  side_boxes[1].lower(2),
                                  side_boxes[1].upper(2),
                                  U_adv_data[1]->getGhostCellWidth()(0),
                                  U_adv_data[1]->getGhostCellWidth()(1),
                                  U_adv_data[1]->getGhostCellWidth()(2),
                                  U_adv_data[1]->getPointer(0),
                                  U_adv_data[1]->getPointer(1),
                                  U_adv_data[1]->getPointer(2),
                                  side_boxes[2].lower(0),
                                  side_boxes[2].upper(0),
                                  side_boxes[2].lower(1),
                                  side_boxes[2].upper(1),
                                  side_boxes[2].lower(2),
                                  side_boxes[2].upper(2),
                                  U_adv_data[2]->getGhostCellWidth()(0),
                                  U_adv_data[2]->getGhostCellWidth()(1),
                                  U_adv_data[2]->getGhostCellWidth()(2),
                                  U_adv_data[2]->getPointer(0),
                                  U_adv_data[2]->getPointer(1),
                                  U_adv_data[2]->getPointer(2));
#endif
} // computeAdvectionVelocity

void
VCINSStaggeredConservativeConvectiveOperator::interpolateSideQuantity(
    boost::array<Pointer<FaceData<NDIM, double> >, NDIM> Q_half_data,
    const boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_adv_data,
    const Pointer<SideData<NDIM, double> > Q_data,
    const IntVector<NDIM>& patch_lower,
    const IntVector<NDIM>& patch_upper,
    const boost::array<Box<NDIM>, NDIM>& side_boxes,
    const VCConvectiveLimiter& convective_limiter)
{
    switch (convective_limiter)
    {
    case VC_UPWIND:
        // Upwind side-centered densities onto faces.
        VC_NAVIER_STOKES_UPWIND_QUANTITY_FC(patch_lower(0),
                                            patch_upper(0),
                                            patch_lower(1),
                                            patch_upper(1),
#if (NDIM == 3)
                                            patch_lower(2),
                                            patch_upper(2),
#endif
                                            Q_data->getGhostCellWidth()(0),
                                            Q_data->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_data->getGhostCellWidth()(2),
#endif
                                            Q_data->getPointer(0),
                                            Q_data->getPointer(1),
#if (NDIM == 3)
                                            Q_data->getPointer(2),
#endif
                                            side_boxes[0].lower(0),
                                            side_boxes[0].upper(0),
                                            side_boxes[0].lower(1),
                                            side_boxes[0].upper(1),
#if (NDIM == 3)
                                            side_boxes[0].lower(2),
                                            side_boxes[0].upper(2),
#endif
                                            U_adv_data[0]->getGhostCellWidth()(0),
                                            U_adv_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            U_adv_data[0]->getGhostCellWidth()(2),
#endif
                                            U_adv_data[0]->getPointer(0),
                                            U_adv_data[0]->getPointer(1),
#if (NDIM == 3)
                                            U_adv_data[0]->getPointer(2),
#endif
                                            Q_half_data[0]->getGhostCellWidth()(0),
                                            Q_half_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_half_data[0]->getGhostCellWidth()(2),
#endif
                                            Q_half_data[0]->getPointer(0),
                                            Q_half_data[0]->getPointer(1),
#if (NDIM == 3)
                                            Q_half_data[0]->getPointer(2),
#endif
                                            side_boxes[1].lower(0),
                                            side_boxes[1].upper(0),
                                            side_boxes[1].lower(1),
                                            side_boxes[1].upper(1),
#if (NDIM == 3)
                                            side_boxes[1].lower(2),
                                            side_boxes[1].upper(2),
#endif
                                            U_adv_data[1]->getGhostCellWidth()(0),
                                            U_adv_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            U_adv_data[1]->getGhostCellWidth()(2),
#endif
                                            U_adv_data[1]->getPointer(0),
                                            U_adv_data[1]->getPointer(1),
#if (NDIM == 3)
                                            U_adv_data[1]->getPointer(2),
#endif
                                            Q_half_data[1]->getGhostCellWidth()(0),
                                            Q_half_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_half_data[1]->getGhostCellWidth()(2),
#endif
                                            Q_half_data[1]->getPointer(0),
                                            Q_half_data[1]->getPointer(1)
#if (NDIM == 3)
                                                ,
                                            Q_half_data[1]->getPointer(2),
                                            side_boxes[2].lower(0),
                                            side_boxes[2].upper(0),
                                            side_boxes[2].lower(1),
                                            side_boxes[2].upper(1),
                                            side_boxes[2].lower(2),
                                            side_boxes[2].upper(2),
                                            U_adv_data[2]->getGhostCellWidth()(0),
                                            U_adv_data[2]->getGhostCellWidth()(1),
                                            U_adv_data[2]->getGhostCellWidth()(2),
                                            U_adv_data[2]->getPointer(0),
                                            U_adv_data[2]->getPointer(1),
                                            U_adv_data[2]->getPointer(2),
                                            Q_half_data[2]->getGhostCellWidth()(0),
                                            Q_half_data[2]->getGhostCellWidth()(1),
                                            Q_half_data[2]->getGhostCellWidth()(2),
                                            Q_half_data[2]->getPointer(0),
                                            Q_half_data[2]->getPointer(1),
                                            Q_half_data[2]->getPointer(2)
#endif
                                                );

        break;
    case VC_CUI:
        // Upwind side-centered densities onto faces.
        VC_NAVIER_STOKES_CUI_QUANTITY_FC(patch_lower(0),
                                         patch_upper(0),
                                         patch_lower(1),
                                         patch_upper(1),
#if (NDIM == 3)
                                         patch_lower(2),
                                         patch_upper(2),
#endif
                                         Q_data->getGhostCellWidth()(0),
                                         Q_data->getGhostCellWidth()(1),
#if (NDIM == 3)
                                         Q_data->getGhostCellWidth()(2),
#endif
                                         Q_data->getPointer(0),
                                         Q_data->getPointer(1),
#if (NDIM == 3)
                                         Q_data->getPointer(2),
#endif
                                         side_boxes[0].lower(0),
                                         side_boxes[0].upper(0),
                                         side_boxes[0].lower(1),
                                         side_boxes[0].upper(1),
#if (NDIM == 3)
                                         side_boxes[0].lower(2),
                                         side_boxes[0].upper(2),
#endif
                                         U_adv_data[0]->getGhostCellWidth()(0),
                                         U_adv_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                         U_adv_data[0]->getGhostCellWidth()(2),
#endif
                                         U_adv_data[0]->getPointer(0),
                                         U_adv_data[0]->getPointer(1),
#if (NDIM == 3)
                                         U_adv_data[0]->getPointer(2),
#endif
                                         Q_half_data[0]->getGhostCellWidth()(0),
                                         Q_half_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                         Q_half_data[0]->getGhostCellWidth()(2),
#endif
                                         Q_half_data[0]->getPointer(0),
                                         Q_half_data[0]->getPointer(1),
#if (NDIM == 3)
                                         Q_half_data[0]->getPointer(2),
#endif
                                         side_boxes[1].lower(0),
                                         side_boxes[1].upper(0),
                                         side_boxes[1].lower(1),
                                         side_boxes[1].upper(1),
#if (NDIM == 3)
                                         side_boxes[1].lower(2),
                                         side_boxes[1].upper(2),
#endif
                                         U_adv_data[1]->getGhostCellWidth()(0),
                                         U_adv_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                         U_adv_data[1]->getGhostCellWidth()(2),
#endif
                                         U_adv_data[1]->getPointer(0),
                                         U_adv_data[1]->getPointer(1),
#if (NDIM == 3)
                                         U_adv_data[1]->getPointer(2),
#endif
                                         Q_half_data[1]->getGhostCellWidth()(0),
                                         Q_half_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                         Q_half_data[1]->getGhostCellWidth()(2),
#endif
                                         Q_half_data[1]->getPointer(0),
                                         Q_half_data[1]->getPointer(1)
#if (NDIM == 3)
                                             ,
                                         Q_half_data[1]->getPointer(2),
                                         side_boxes[2].lower(0),
                                         side_boxes[2].upper(0),
                                         side_boxes[2].lower(1),
                                         side_boxes[2].upper(1),
                                         side_boxes[2].lower(2),
                                         side_boxes[2].upper(2),
                                         U_adv_data[2]->getGhostCellWidth()(0),
                                         U_adv_data[2]->getGhostCellWidth()(1),
                                         U_adv_data[2]->getGhostCellWidth()(2),
                                         U_adv_data[2]->getPointer(0),
                                         U_adv_data[2]->getPointer(1),
                                         U_adv_data[2]->getPointer(2),
                                         Q_half_data[2]->getGhostCellWidth()(0),
                                         Q_half_data[2]->getGhostCellWidth()(1),
                                         Q_half_data[2]->getGhostCellWidth()(2),
                                         Q_half_data[2]->getPointer(0),
                                         Q_half_data[2]->getPointer(1),
                                         Q_half_data[2]->getPointer(2)
#endif
                                             );

        break;
    case VC_FBICS:
        VC_NAVIER_STOKES_FBICS_QUANTITY_FC(patch_lower(0),
                                           patch_upper(0),
                                           patch_lower(1),
                                           patch_upper(1),
#if (NDIM == 3)
                                           patch_lower(2),
                                           patch_upper(2),
#endif
                                           Q_data->getGhostCellWidth()(0),
                                           Q_data->getGhostCellWidth()(1),
#if (NDIM == 3)
                                           Q_data->getGhostCellWidth()(2),
#endif
                                           Q_data->getPointer(0),
                                           Q_data->getPointer(1),
#if (NDIM == 3)
                                           Q_data->getPointer(2),
#endif
                                           side_boxes[0].lower(0),
                                           side_boxes[0].upper(0),
                                           side_boxes[0].lower(1),
                                           side_boxes[0].upper(1),
#if (NDIM == 3)
                                           side_boxes[0].lower(2),
                                           side_boxes[0].upper(2),
#endif
                                           U_adv_data[0]->getGhostCellWidth()(0),
                                           U_adv_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                           U_adv_data[0]->getGhostCellWidth()(2),
#endif
                                           U_adv_data[0]->getPointer(0),
                                           U_adv_data[0]->getPointer(1),
#if (NDIM == 3)
                                           U_adv_data[0]->getPointer(2),
#endif
                                           Q_half_data[0]->getGhostCellWidth()(0),
                                           Q_half_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                           Q_half_data[0]->getGhostCellWidth()(2),
#endif
                                           Q_half_data[0]->getPointer(0),
                                           Q_half_data[0]->getPointer(1),
#if (NDIM == 3)
                                           Q_half_data[0]->getPointer(2),
#endif
                                           side_boxes[1].lower(0),
                                           side_boxes[1].upper(0),
                                           side_boxes[1].lower(1),
                                           side_boxes[1].upper(1),
#if (NDIM == 3)
                                           side_boxes[1].lower(2),
                                           side_boxes[1].upper(2),
#endif
                                           U_adv_data[1]->getGhostCellWidth()(0),
                                           U_adv_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                           U_adv_data[1]->getGhostCellWidth()(2),
#endif
                                           U_adv_data[1]->getPointer(0),
                                           U_adv_data[1]->getPointer(1),
#if (NDIM == 3)
                                           U_adv_data[1]->getPointer(2),
#endif
                                           Q_half_data[1]->getGhostCellWidth()(0),
                                           Q_half_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                           Q_half_data[1]->getGhostCellWidth()(2),
#endif
                                           Q_half_data[1]->getPointer(0),
                                           Q_half_data[1]->getPointer(1)
#if (NDIM == 3)
                                               ,
                                           Q_half_data[1]->getPointer(2),
                                           side_boxes[2].lower(0),
                                           side_boxes[2].upper(0),
                                           side_boxes[2].lower(1),
                                           side_boxes[2].upper(1),
                                           side_boxes[2].lower(2),
                                           side_boxes[2].upper(2),
                                           U_adv_data[2]->getGhostCellWidth()(0),
                                           U_adv_data[2]->getGhostCellWidth()(1),
                                           U_adv_data[2]->getGhostCellWidth()(2),
                                           U_adv_data[2]->getPointer(0),
                                           U_adv_data[2]->getPointer(1),
                                           U_adv_data[2]->getPointer(2),
                                           Q_half_data[2]->getGhostCellWidth()(0),
                                           Q_half_data[2]->getGhostCellWidth()(1),
                                           Q_half_data[2]->getGhostCellWidth()(2),
                                           Q_half_data[2]->getPointer(0),
                                           Q_half_data[2]->getPointer(1),
                                           Q_half_data[2]->getPointer(2)
#endif
                                               );
        break;
    case VC_MGAMMA:
        // Upwind side-centered densities onto faces.
        VC_NAVIER_STOKES_MGAMMA_QUANTITY_FC(patch_lower(0),
                                            patch_upper(0),
                                            patch_lower(1),
                                            patch_upper(1),
#if (NDIM == 3)
                                            patch_lower(2),
                                            patch_upper(2),
#endif
                                            Q_data->getGhostCellWidth()(0),
                                            Q_data->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_data->getGhostCellWidth()(2),
#endif
                                            Q_data->getPointer(0),
                                            Q_data->getPointer(1),
#if (NDIM == 3)
                                            Q_data->getPointer(2),
#endif
                                            side_boxes[0].lower(0),
                                            side_boxes[0].upper(0),
                                            side_boxes[0].lower(1),
                                            side_boxes[0].upper(1),
#if (NDIM == 3)
                                            side_boxes[0].lower(2),
                                            side_boxes[0].upper(2),
#endif
                                            U_adv_data[0]->getGhostCellWidth()(0),
                                            U_adv_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            U_adv_data[0]->getGhostCellWidth()(2),
#endif
                                            U_adv_data[0]->getPointer(0),
                                            U_adv_data[0]->getPointer(1),
#if (NDIM == 3)
                                            U_adv_data[0]->getPointer(2),
#endif
                                            Q_half_data[0]->getGhostCellWidth()(0),
                                            Q_half_data[0]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_half_data[0]->getGhostCellWidth()(2),
#endif
                                            Q_half_data[0]->getPointer(0),
                                            Q_half_data[0]->getPointer(1),
#if (NDIM == 3)
                                            Q_half_data[0]->getPointer(2),
#endif
                                            side_boxes[1].lower(0),
                                            side_boxes[1].upper(0),
                                            side_boxes[1].lower(1),
                                            side_boxes[1].upper(1),
#if (NDIM == 3)
                                            side_boxes[1].lower(2),
                                            side_boxes[1].upper(2),
#endif
                                            U_adv_data[1]->getGhostCellWidth()(0),
                                            U_adv_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            U_adv_data[1]->getGhostCellWidth()(2),
#endif
                                            U_adv_data[1]->getPointer(0),
                                            U_adv_data[1]->getPointer(1),
#if (NDIM == 3)
                                            U_adv_data[1]->getPointer(2),
#endif
                                            Q_half_data[1]->getGhostCellWidth()(0),
                                            Q_half_data[1]->getGhostCellWidth()(1),
#if (NDIM == 3)
                                            Q_half_data[1]->getGhostCellWidth()(2),
#endif
                                            Q_half_data[1]->getPointer(0),
                                            Q_half_data[1]->getPointer(1)
#if (NDIM == 3)
                                                ,
                                            Q_half_data[1]->getPointer(2),
                                            side_boxes[2].lower(0),
                                            side_boxes[2].upper(0),
                                            side_boxes[2].lower(1),
                                            side_boxes[2].upper(1),
                                            side_boxes[2].lower(2),
                                            side_boxes[2].upper(2),
                                            U_adv_data[2]->getGhostCellWidth()(0),
                                            U_adv_data[2]->getGhostCellWidth()(1),
                                            U_adv_data[2]->getGhostCellWidth()(2),
                                            U_adv_data[2]->getPointer(0),
                                            U_adv_data[2]->getPointer(1),
                                            U_adv_data[2]->getPointer(2),
                                            Q_half_data[2]->getGhostCellWidth()(0),
                                            Q_half_data[2]->getGhostCellWidth()(1),
                                            Q_half_data[2]->getGhostCellWidth()(2),
                                            Q_half_data[2]->getPointer(0),
                                            Q_half_data[2]->getPointer(1),
                                            Q_half_data[2]->getPointer(2)
#endif
                                                );
        break;

    default:
        TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator():\n"
                   << "  unsupported convective limiter: "
                   << IBAMR::enum_to_string<VCConvectiveLimiter>(convective_limiter)
                   << " \n"
                   << "  valid choices are: VC_UPWIND, VC_CUI, VC_FBICS, VC_MGAMMA\n");
    }
} // interpolateSideQuantity

void
VCINSStaggeredConservativeConvectiveOperator::computeConvectiveDerivative(
    Pointer<SideData<NDIM, double> > N_data,
    boost::array<Pointer<FaceData<NDIM, double> >, NDIM> P_half_data,
    const boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_adv_data,
    const boost::array<Pointer<FaceData<NDIM, double> >, NDIM> R_half_data,
    const boost::array<Pointer<FaceData<NDIM, double> >, NDIM> U_half_data,
    const boost::array<Box<NDIM>, NDIM>& side_boxes,
    const double* const dx)
{
// Compute the upwinded momentum P_half = R_half * U_half
#if (NDIM == 2)
    VC_NAVIER_STOKES_COMPUTE_MOMENTUM_FC(side_boxes[0].lower(0),
                                         side_boxes[0].upper(0),
                                         side_boxes[0].lower(1),
                                         side_boxes[0].upper(1),
                                         P_half_data[0]->getGhostCellWidth()(0),
                                         P_half_data[0]->getGhostCellWidth()(1),
                                         P_half_data[0]->getPointer(0),
                                         P_half_data[0]->getPointer(1),
                                         R_half_data[0]->getGhostCellWidth()(0),
                                         R_half_data[0]->getGhostCellWidth()(1),
                                         R_half_data[0]->getPointer(0),
                                         R_half_data[0]->getPointer(1),
                                         U_half_data[0]->getGhostCellWidth()(0),
                                         U_half_data[0]->getGhostCellWidth()(1),
                                         U_half_data[0]->getPointer(0),
                                         U_half_data[0]->getPointer(1),
                                         side_boxes[1].lower(0),
                                         side_boxes[1].upper(0),
                                         side_boxes[1].lower(1),
                                         side_boxes[1].upper(1),
                                         P_half_data[1]->getGhostCellWidth()(0),
                                         P_half_data[1]->getGhostCellWidth()(1),
                                         P_half_data[1]->getPointer(0),
                                         P_half_data[1]->getPointer(1),
                                         R_half_data[1]->getGhostCellWidth()(0),
                                         R_half_data[1]->getGhostCellWidth()(1),
                                         R_half_data[1]->getPointer(0),
                                         R_half_data[1]->getPointer(1),
                                         U_half_data[1]->getGhostCellWidth()(0),
                                         U_half_data[1]->getGhostCellWidth()(1),
                                         U_half_data[1]->getPointer(0),
                                         U_half_data[1]->getPointer(1));
#endif
#if (NDIM == 3)
    VC_NAVIER_STOKES_COMPUTE_MOMENTUM_FC(side_boxes[0].lower(0),
                                         side_boxes[0].upper(0),
                                         side_boxes[0].lower(1),
                                         side_boxes[0].upper(1),
                                         side_boxes[0].lower(2),
                                         side_boxes[0].upper(2),
                                         P_half_data[0]->getGhostCellWidth()(0),
                                         P_half_data[0]->getGhostCellWidth()(1),
                                         P_half_data[0]->getGhostCellWidth()(2),
                                         P_half_data[0]->getPointer(0),
                                         P_half_data[0]->getPointer(1),
                                         P_half_data[0]->getPointer(2),
                                         R_half_data[0]->getGhostCellWidth()(0),
                                         R_half_data[0]->getGhostCellWidth()(1),
                                         R_half_data[0]->getGhostCellWidth()(2),
                                         R_half_data[0]->getPointer(0),
                                         R_half_data[0]->getPointer(1),
                                         R_half_data[0]->getPointer(2),
                                         U_half_data[0]->getGhostCellWidth()(0),
                                         U_half_data[0]->getGhostCellWidth()(1),
                                         U_half_data[0]->getGhostCellWidth()(2),
                                         U_half_data[0]->getPointer(0),
                                         U_half_data[0]->getPointer(1),
                                         U_half_data[0]->getPointer(2),
                                         side_boxes[1].lower(0),
                                         side_boxes[1].upper(0),
                                         side_boxes[1].lower(1),
                                         side_boxes[1].upper(1),
                                         side_boxes[1].lower(2),
                                         side_boxes[1].upper(2),
                                         P_half_data[1]->getGhostCellWidth()(0),
                                         P_half_data[1]->getGhostCellWidth()(1),
                                         P_half_data[1]->getGhostCellWidth()(2),
                                         P_half_data[1]->getPointer(0),
                                         P_half_data[1]->getPointer(1),
                                         P_half_data[1]->getPointer(2),
                                         R_half_data[1]->getGhostCellWidth()(0),
                                         R_half_data[1]->getGhostCellWidth()(1),
                                         R_half_data[1]->getGhostCellWidth()(2),
                                         R_half_data[1]->getPointer(0),
                                         R_half_data[1]->getPointer(1),
                                         R_half_data[1]->getPointer(2),
                                         U_half_data[1]->getGhostCellWidth()(0),
                                         U_half_data[1]->getGhostCellWidth()(1),
                                         U_half_data[1]->getGhostCellWidth()(2),
                                         U_half_data[1]->getPointer(0),
                                         U_half_data[1]->getPointer(1),
                                         U_half_data[1]->getPointer(2),
                                         side_boxes[2].lower(0),
                                         side_boxes[2].upper(0),
                                         side_boxes[2].lower(1),
                                         side_boxes[2].upper(1),
                                         side_boxes[2].lower(2),
                                         side_boxes[2].upper(2),
                                         P_half_data[2]->getGhostCellWidth()(0),
                                         P_half_data[2]->getGhostCellWidth()(1),
                                         P_half_data[2]->getGhostCellWidth()(2),
                                         P_half_data[2]->getPointer(0),
                                         P_half_data[2]->getPointer(1),
                                         P_half_data[2]->getPointer(2),
                                         R_half_data[2]->getGhostCellWidth()(0),
                                         R_half_data[2]->getGhostCellWidth()(1),
                                         R_half_data[2]->getGhostCellWidth()(2),
                                         R_half_data[2]->getPointer(0),
                                         R_half_data[2]->getPointer(1),
                                         R_half_data[2]->getPointer(2),
                                         U_half_data[2]->getGhostCellWidth()(0),
                                         U_half_data[2]->getGhostCellWidth()(1),
                                         U_half_data[2]->getGhostCellWidth()(2),
                                         U_half_data[2]->getPointer(0),
                                         U_half_data[2]->getPointer(1),
                                         U_half_data[2]->getPointer(2));
#endif

    for (unsigned int axis = 0; axis < NDIM; ++axis)
    {
        switch (d_difference_form)
        {
        case CONSERVATIVE:
#if (NDIM == 2)
            CONVECT_DERIVATIVE_FC(dx,
                                  side_boxes[axis].lower(0),
                                  side_boxes[axis].upper(0),
                                  side_boxes[axis].lower(1),
                                  side_boxes[axis].upper(1),
                                  U_adv_data[axis]->getGhostCellWidth()(0),
                                  U_adv_data[axis]->getGhostCellWidth()(1),
                                  P_half_data[axis]->getGhostCellWidth()(0),
                                  P_half_data[axis]->getGhostCellWidth()(1),
                                  U_adv_data[axis]->getPointer(0),
                                  U_adv_data[axis]->getPointer(1),
                                  P_half_data[axis]->getPointer(0),
                                  P_half_data[axis]->getPointer(1),
                                  N_data->getGhostCellWidth()(0),
                                  N_data->getGhostCellWidth()(1),
                                  N_data->getPointer(axis));
#endif
#if (NDIM == 3)
            CONVECT_DERIVATIVE_FC(dx,
                                  side_boxes[axis].lower(0),
                                  side_boxes[axis].upper(0),
                                  side_boxes[axis].lower(1),
                                  side_boxes[axis].upper(1),
                                  side_boxes[axis].lower(2),
                                  side_boxes[axis].upper(2),
                                  U_adv_data[axis]->getGhostCellWidth()(0),
                                  U_adv_data[axis]->getGhostCellWidth()(1),
                                  U_adv_data[axis]->getGhostCellWidth()(2),
                                  P_half_data[axis]->getGhostCellWidth()(0),
                                  P_half_data[axis]->getGhostCellWidth()(1),
                                  P_half_data[axis]->getGhostCellWidth()(2),
                                  U_adv_data[axis]->getPointer(0),
                                  U_adv_data[axis]->getPointer(1),
                                  U_adv_data[axis]->getPointer(2),
                                  P_half_data[axis]->getPointer(0),
                                  P_half_data[axis]->getPointer(1),
                                  P_half_data[axis]->getPointer(2),
                                  N_data->getGhostCellWidth()(0),
                                  N_data->getGhostCellWidth()(1),
                                  N_data->getGhostCellWidth()(2),
                                  N_data->getPointer(axis));
#endif
            break;
        default:
            TBOX_ERROR("VCINSStaggeredConservativeConvectiveOperator::applyConvectiveOperator():\n"
                       << "  unsupported differencing form: "
                       << enum_to_string<ConvectiveDifferencingType>(d_difference_form)
                       << " \n"
                       << "  valid choices are: CONSERVATIVE\n");
        }
    }
} // computeConvectiveDerivative

#if 0
            // Correct density for inflow conditions.
            if (patch_geom->getTouchesRegularBoundary())
            {
                // Compute the co-dimension one boundary boxes.
                const Array<BoundaryBox<NDIM> > physical_codim1_boxes =
                PhysicalBoundaryUtilities::getPhysicalBoundaryCodim1Boxes(*patch);
                
                // There is nothing to do if the patch does not have any co-dimension one
                // boundary boxes.
                if (physical_codim1_boxes.size() == 0) break;
                
                // Created shifted patch geometry.
                const double* const patch_x_lower = patch_geom->getXLower();
                const double* const patch_x_upper = patch_geom->getXUpper();
                const IntVector<NDIM>& ratio_to_level_zero = patch_geom->getRatio();
                Array<Array<bool> > touches_regular_bdry(NDIM), touches_periodic_bdry(NDIM);
                for (unsigned int axis = 0; axis < NDIM; ++axis)
                {
                    touches_regular_bdry[axis].resizeArray(2);
                    touches_periodic_bdry[axis].resizeArray(2);
                    for (int upperlower = 0; upperlower < 2; ++upperlower)
                    {
                        touches_regular_bdry[axis][upperlower] = patch_geom->getTouchesRegularBoundary(axis, upperlower);
                        touches_periodic_bdry[axis][upperlower] = patch_geom->getTouchesPeriodicBoundary(axis, upperlower);
                    }
                }
                
                // Set the mass influx at inflow boundaries.
                for (unsigned int axis = 0; axis < NDIM; ++axis)
                {
                    for (int n = 0; n < physical_codim1_boxes.size(); ++n)
                    {
                        const BoundaryBox<NDIM>& bdry_box = physical_codim1_boxes[n];
                        const unsigned int location_index = bdry_box.getLocationIndex();
                        const unsigned int bdry_normal_axis = location_index / 2;
                        const bool is_lower = location_index % 2 == 0;
                        
                        static const IntVector<NDIM> gcw_to_fill = 1;
                        const Box<NDIM> bc_fill_box = patch_geom->getBoundaryFillBox(bdry_box, patch_box, gcw_to_fill);
                        const BoundaryBox<NDIM> trimmed_bdry_box(
                                                                 bdry_box.getBox() * bc_fill_box, bdry_box.getBoundaryType(), bdry_box.getLocationIndex());
                        const Box<NDIM> bc_coef_box = PhysicalBoundaryUtilities::makeSideBoundaryCodim1Box(trimmed_bdry_box);
                        
                        Pointer<ArrayData<NDIM, double> > acoef_data = new ArrayData<NDIM, double>(bc_coef_box, 1);
                        Pointer<ArrayData<NDIM, double> > bcoef_data = new ArrayData<NDIM, double>(bc_coef_box, 1);
                        Pointer<ArrayData<NDIM, double> > gcoef_data = new ArrayData<NDIM, double>(bc_coef_box, 1);
                        
                        
                        if (axis != bdry_normal_axis)
                        {
                            // Temporarily reset the patch geometry object associated with the
                            // patch so that boundary conditions are set at the correct spatial
                            // locations.
                            boost::array<double, NDIM> shifted_patch_x_lower, shifted_patch_x_upper;
                            for (unsigned int d = 0; d < NDIM; ++d)
                            {
                                shifted_patch_x_lower[d] = patch_x_lower[d];
                                shifted_patch_x_upper[d] = patch_x_upper[d];
                            }
                            shifted_patch_x_lower[axis] -= 0.5 * dx[axis];
                            shifted_patch_x_upper[axis] -= 0.5 * dx[axis];
                            patch->setPatchGeometry(new CartesianPatchGeometry<NDIM>(ratio_to_level_zero,
                                                                                     touches_regular_bdry,
                                                                                     touches_periodic_bdry,
                                                                                     dx,
                                                                                     shifted_patch_x_lower.data(),
                                                                                     shifted_patch_x_upper.data()));
                        }
    
                        d_rho_interp_bc_coefs[bdry_normal_axis]->setBcCoefs(acoef_data, bcoef_data, gcoef_data,Pointer<Variable<NDIM> >(NULL), *patch, trimmed_bdry_box, d_current_time);
                        
                        // Restore the original patch geometry object.
                        patch->setPatchGeometry(patch_geom);
                        
                        for (Box<NDIM>::Iterator b(bc_coef_box); b; b++)
                        {
                            const Index<NDIM>& i = b();
                            const FaceIndex<NDIM> i_f(i, bdry_normal_axis, FaceIndex<NDIM>::Lower);
                            const double inflow_vel = (*U_half_data[axis])(i_f);
                            //const SideIndex<NDIM> i_s(i, bdry_normal_axis, SideIndex<NDIM>::Lower);
                            //const double inflow_vel = (*U_data)(i_s);
                            
                            bool is_inflow_bdry = (is_lower && inflow_vel > 0.0) || (!is_lower && inflow_vel < 0.0);
                            if (is_inflow_bdry)
                            {
                                const double& a = (*acoef_data)(i, 0);
                                const double& b = (*bcoef_data)(i, 0);
                                const double& g = (*gcoef_data)(i, 0);
                                const double& h = dx[bdry_normal_axis];
                                TBOX_ASSERT(MathUtilities<double>::equalEps(b, 0));
                                TBOX_ASSERT(MathUtilities<double>::equalEps(a, 1.0));
                                
                                Index<NDIM> i_intr(i);
                                if (is_lower)
                                {
                                    // intentionally left blank
                                }
                                else
                                {
                                    i_intr(bdry_normal_axis) -= 1;
                                }
                                
                                
                                const FaceIndex<NDIM> i_f_intr(
                                                               i_intr, bdry_normal_axis, (is_lower ? FaceIndex<NDIM>::Upper : FaceIndex<NDIM>::Lower));
                                const FaceIndex<NDIM> i_f_bdry(
                                                               i_intr, bdry_normal_axis, (is_lower ? FaceIndex<NDIM>::Lower : FaceIndex<NDIM>::Upper));
                                
                                const double& P_adv_i = (*P_adv_data[axis])(i_f_intr, /*depth*/0);
                                const double P_adv_b = (b * P_adv_i + g * inflow_vel * h) / (a * h + b);
                                (*P_adv_data[axis])(i_f_bdry, /*depth*/0) = P_adv_b;
                            }
                        }
                    }
                }
            }
#endif

//////////////////////////////////////////////////////////////////////////////

} // namespace IBAMR

//////////////////////////////////////////////////////////////////////////////
