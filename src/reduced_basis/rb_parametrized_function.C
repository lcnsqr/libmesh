// rbOOmit: An implementation of the Certified Reduced Basis method.
// Copyright (C) 2009, 2010 David J. Knezevic

// This file is part of rbOOmit.

// rbOOmit is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// rbOOmit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// libmesh includes
#include "libmesh/rb_parametrized_function.h"
#include "libmesh/int_range.h"
#include "libmesh/point.h"
#include "libmesh/libmesh_logging.h"
#include "libmesh/utility.h"
#include "libmesh/rb_parameters.h"
#include "libmesh/system.h"
#include "libmesh/elem.h"
#include "libmesh/fem_context.h"

namespace libMesh
{

RBParametrizedFunction::RBParametrizedFunction()
:
requires_xyz_perturbations(false),
is_lookup_table(false),
fd_delta(1.e-6)
{}

RBParametrizedFunction::~RBParametrizedFunction() = default;

Number
RBParametrizedFunction::evaluate_comp(const RBParameters & mu,
                                      unsigned int comp,
                                      const Point & xyz,
                                      dof_id_type elem_id,
                                      unsigned int qp,
                                      subdomain_id_type subdomain_id,
                                      const std::vector<Point> & xyz_perturb,
                                      const std::vector<Real> & phi_i_qp)
{
  std::vector<Number> values = evaluate(mu, xyz, elem_id, qp, subdomain_id, xyz_perturb, phi_i_qp);

  libmesh_error_msg_if(comp >= values.size(), "Error: Invalid value of comp");

  return values[comp];
}

void RBParametrizedFunction::vectorized_evaluate(const std::vector<RBParameters> & mus,
                                                 const std::vector<Point> & all_xyz,
                                                 const std::vector<dof_id_type> & elem_ids,
                                                 const std::vector<unsigned int> & qps,
                                                 const std::vector<subdomain_id_type> & sbd_ids,
                                                 const std::vector<std::vector<Point>> & all_xyz_perturb,
                                                 const std::vector<std::vector<Real>> & phi_i_qp,
                                                 std::vector<std::vector<std::vector<Number>>> & output)
{
  LOG_SCOPE("vectorized_evaluate()", "RBParametrizedFunction");

  output.clear();
  unsigned int n_points = all_xyz.size();

  libmesh_error_msg_if(sbd_ids.size() != n_points, "Error: invalid vector sizes");
  libmesh_error_msg_if(requires_xyz_perturbations && (all_xyz_perturb.size() != n_points), "Error: invalid vector sizes");

  // Dummy vector to be used when xyz perturbations are not required
  std::vector<Point> empty_perturbs;

  output.resize(mus.size());
  for ( unsigned int mu_index : index_range(mus))
    {
      output[mu_index].resize(n_points);
      for (unsigned int point_index=0; point_index<n_points; point_index++)
        {
          if (requires_xyz_perturbations)
            {
              output[mu_index][point_index] = evaluate(mus[mu_index],
                                                       all_xyz[point_index],
                                                       elem_ids[point_index],
                                                       qps[point_index],
                                                       sbd_ids[point_index],
                                                       all_xyz_perturb[point_index],
                                                       phi_i_qp[point_index]);
            }
          else
            {
              output[mu_index][point_index] = evaluate(mus[mu_index],
                                                       all_xyz[point_index],
                                                       elem_ids[point_index],
                                                       qps[point_index],
                                                       sbd_ids[point_index],
                                                       empty_perturbs,
                                                       phi_i_qp[point_index]);
            }
        }
    }
}

void RBParametrizedFunction::preevaluate_parametrized_function_on_mesh(const RBParameters & mu,
                                                                       const std::unordered_map<dof_id_type, std::vector<Point>> & all_xyz,
                                                                       const std::unordered_map<dof_id_type, subdomain_id_type> & sbd_ids,
                                                                       const std::unordered_map<dof_id_type, std::vector<std::vector<Point>> > & all_xyz_perturb,
                                                                       const System & sys)
{
  mesh_to_preevaluated_values_map.clear();

  unsigned int n_points = 0;
  for (const auto & xyz_pair : all_xyz)
  {
    const std::vector<Point> & xyz_vec = xyz_pair.second;
    n_points += xyz_vec.size();
  }

  std::vector<Point> all_xyz_vec(n_points);
  std::vector<dof_id_type> elem_ids_vec(n_points);
  std::vector<unsigned int> qps_vec(n_points);
  std::vector<subdomain_id_type> sbd_ids_vec(n_points);
  std::vector<std::vector<Point>> all_xyz_perturb_vec(n_points);
  std::vector<std::vector<Real>> phi_i_qp_vec(n_points);

  // Empty vector to be used when xyz perturbations are not required
  std::vector<Point> empty_perturbs;

  // In order to compute phi_i_qp, we initialize a FEMContext
  FEMContext con(sys);
  for (auto dim : con.elem_dimensions())
    {
      auto fe = con.get_element_fe(/*var=*/0, dim);
      fe->get_phi();
    }

  unsigned int counter = 0;
  for (const auto & xyz_pair : all_xyz)
    {
      dof_id_type elem_id = xyz_pair.first;
      const std::vector<Point> & xyz_vec = xyz_pair.second;

      subdomain_id_type subdomain_id = libmesh_map_find(sbd_ids, elem_id);

      // The amount of data to be stored for each component
      auto n_qp = xyz_vec.size();
      mesh_to_preevaluated_values_map[elem_id].resize(n_qp);

      // Also initialize phi in order to compute phi_i_qp
      const Elem & elem_ref = sys.get_mesh().elem_ref(elem_id);
      con.pre_fe_reinit(sys, &elem_ref);

      auto elem_fe = con.get_element_fe(/*var=*/0, elem_ref.dim());
      const std::vector<std::vector<Real>> & phi = elem_fe->get_phi();

      elem_fe->reinit(&elem_ref);

      for (auto qp : index_range(xyz_vec))
        {
          mesh_to_preevaluated_values_map[elem_id][qp] = counter;

          all_xyz_vec[counter] = xyz_vec[qp];
          elem_ids_vec[counter] = elem_id;
          qps_vec[counter] = qp;
          sbd_ids_vec[counter] = subdomain_id;

          phi_i_qp_vec[counter].resize(phi.size());
          for(auto i : index_range(phi))
            phi_i_qp_vec[counter][i] = phi[i][qp];

          if (requires_xyz_perturbations)
            {
              const auto & qps_and_perturbs =
                libmesh_map_find(all_xyz_perturb, elem_id);
              libmesh_error_msg_if(qp >= qps_and_perturbs.size(), "Error: Invalid qp");

              all_xyz_perturb_vec[counter] = qps_and_perturbs[qp];
            }
          else
            {
              all_xyz_perturb_vec[counter] = empty_perturbs;
            }

          counter++;
        }
    }

  std::vector<RBParameters> mus {mu};
  vectorized_evaluate(mus,
                      all_xyz_vec,
                      elem_ids_vec,
                      qps_vec,
                      sbd_ids_vec,
                      all_xyz_perturb_vec,
                      phi_i_qp_vec,
                      preevaluated_values);
}

Number RBParametrizedFunction::lookup_preevaluated_value_on_mesh(unsigned int comp,
                                                                 dof_id_type elem_id,
                                                                 unsigned int qp) const
{
  const std::vector<unsigned int> & indices_at_qps =
    libmesh_map_find(mesh_to_preevaluated_values_map, elem_id);

  libmesh_error_msg_if(qp >= indices_at_qps.size(), "Error: invalid qp");

  unsigned int index = indices_at_qps[qp];
  libmesh_error_msg_if(preevaluated_values.size() != 1, "Error: we expect only one parameter index");
  libmesh_error_msg_if(index >= preevaluated_values[0].size(), "Error: invalid index");

  return preevaluated_values[0][index][comp];
}

void RBParametrizedFunction::initialize_lookup_table()
{
  // No-op by default, override in subclasses as needed
}

Number RBParametrizedFunction::get_parameter_independent_data(const std::string & property_name,
                                                              subdomain_id_type sbd_id) const
{
  return libmesh_map_find(libmesh_map_find(_parameter_independent_data, property_name), sbd_id);
}

std::vector<std::vector<Number>> RBParametrizedFunction::evaluate_at_observation_points(const RBParameters & /*mu*/,
                                                                                        const std::vector<Point> & /*observation_points*/,
                                                                                        const std::vector<dof_id_type> & /*elem_ids*/,
                                                                                        const std::vector<subdomain_id_type> & /*sbd_ids*/)
{
  // return an empty vector by default, override this in subclasses if necessary
  return std::vector<std::vector<Number>>();
}

}
