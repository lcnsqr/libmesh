// The libMesh Finite Element Library.
// Copyright (C) 2002-2021 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



// Local Includes
#include "libmesh/mesh_function.h"
#include "libmesh/dense_vector.h"
#include "libmesh/equation_systems.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/dof_map.h"
#include "libmesh/point_locator_tree.h"
#include "libmesh/fe_base.h"
#include "libmesh/fe_interface.h"
#include "libmesh/fe_compute_data.h"
#include "libmesh/mesh_base.h"
#include "libmesh/point.h"
#include "libmesh/elem.h"
#include "libmesh/int_range.h"
#include "libmesh/fe_map.h"

namespace libMesh
{


//------------------------------------------------------------------
// MeshFunction methods
MeshFunction::MeshFunction (const EquationSystems & eqn_systems,
                            const NumericVector<Number> & vec,
                            const DofMap & dof_map,
                            const std::vector<unsigned int> & vars,
                            const FunctionBase<Number> * master) :
  FunctionBase<Number> (master),
  ParallelObject       (eqn_systems),
  _eqn_systems         (eqn_systems),
  _vector              (vec),
  _dof_map             (dof_map),
  _system_vars         (vars),
  _out_of_mesh_mode    (false)
{
}



MeshFunction::MeshFunction (const EquationSystems & eqn_systems,
                            const NumericVector<Number> & vec,
                            const DofMap & dof_map,
                            const unsigned int var,
                            const FunctionBase<Number> * master) :
  FunctionBase<Number> (master),
  ParallelObject       (eqn_systems),
  _eqn_systems         (eqn_systems),
  _vector              (vec),
  _dof_map             (dof_map),
  _system_vars         (1,var),
  _out_of_mesh_mode    (false)
{
}

MeshFunction::MeshFunction (const MeshFunction & mf):
  FunctionBase<Number> (mf._master),
  ParallelObject       (mf._eqn_systems),
  _eqn_systems         (mf._eqn_systems),
  _vector              (mf._vector),
  _dof_map             (mf._dof_map),
  _system_vars         (mf._system_vars),
  _out_of_mesh_mode    (mf._out_of_mesh_mode)
{
  // Initialize the mf and set the point locator if the
  // input mf had done so.
  if(mf.initialized())
  {
    this->init();

    if(mf.get_point_locator().initialized())
      this->set_point_locator_tolerance(mf.get_point_locator().get_close_to_point_tol());

  }

  if (mf._subdomain_ids)
    _subdomain_ids =
      libmesh_make_unique<std::set<subdomain_id_type>>
        (*mf._subdomain_ids);
}

MeshFunction::~MeshFunction () = default;



void MeshFunction::init ()
{
  // are indices of the desired variable(s) provided?
  libmesh_assert_greater (this->_system_vars.size(), 0);

  // Don't do twice...
  if (this->_initialized)
    {
      libmesh_assert(_point_locator);
      return;
    }

  // The Mesh owns the "master" PointLocator, while handing us a
  // PointLocator "proxy" that forwards all requests to the master.
  const MeshBase & mesh = this->_eqn_systems.get_mesh();
  _point_locator = mesh.sub_point_locator();

  // ready for use
  this->_initialized = true;
}



void MeshFunction::init (const Trees::BuildType /*point_locator_build_type*/)
{
  libmesh_deprecated();

  // Call the init() taking no args instead. Note: this is backwards
  // compatible because the argument was not used for anything
  // previously anyway.
  this->init();
}



void
MeshFunction::clear ()
{
  // only delete the point locator when we are the master
  if (_point_locator && !_master)
    _point_locator.reset();

  this->_initialized = false;
}



std::unique_ptr<FunctionBase<Number>> MeshFunction::clone () const
{
  return libmesh_make_unique<MeshFunction>(*this);
}



Number MeshFunction::operator() (const Point & p,
                                 const Real time)
{
  libmesh_assert (this->initialized());

  DenseVector<Number> buf (1);
  this->operator() (p, time, buf);
  return buf(0);
}

std::map<const Elem *, Number> MeshFunction::discontinuous_value (const Point & p,
                                                                  const Real time)
{
  libmesh_assert (this->initialized());

  std::map<const Elem *, DenseVector<Number>> buffer;
  this->discontinuous_value (p, time, buffer);
  std::map<const Elem *, Number> return_value;
  for (const auto & pr : buffer)
    return_value[pr.first] = pr.second(0);
  // NOTE: If no suitable element is found, then the map return_value is empty. This
  // puts burden on the user of this function but I don't really see a better way.
  return return_value;
}

Gradient MeshFunction::gradient (const Point & p,
                                 const Real time)
{
  libmesh_assert (this->initialized());

  std::vector<Gradient> buf (1);
  this->gradient(p, time, buf);
  return buf[0];
}

std::map<const Elem *, Gradient> MeshFunction::discontinuous_gradient (const Point & p,
                                                                       const Real time)
{
  libmesh_assert (this->initialized());

  std::map<const Elem *, std::vector<Gradient>> buffer;
  this->discontinuous_gradient (p, time, buffer);
  std::map<const Elem *, Gradient> return_value;
  for (const auto & pr : buffer)
    return_value[pr.first] = pr.second[0];
  // NOTE: If no suitable element is found, then the map return_value is empty. This
  // puts burden on the user of this function but I don't really see a better way.
  return return_value;
}

#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
Tensor MeshFunction::hessian (const Point & p,
                              const Real time)
{
  libmesh_assert (this->initialized());

  std::vector<Tensor> buf (1);
  this->hessian(p, time, buf);
  return buf[0];
}
#endif

void MeshFunction::operator() (const Point & p,
                               const Real time,
                               DenseVector<Number> & output)
{
  this->operator() (p, time, output, this->_subdomain_ids.get());
}

void MeshFunction::operator() (const Point & p,
                               const Real,
                               DenseVector<Number> & output,
                               const std::set<subdomain_id_type> * subdomain_ids)
{
  libmesh_assert (this->initialized());

  const Elem * element = this->find_element(p,subdomain_ids);

  if (!element)
    {
      // We'd better be in out_of_mesh_mode if we couldn't find an
      // element in the mesh
      libmesh_assert (_out_of_mesh_mode);
      output = _out_of_mesh_value;
    }
  else
    {
      // resize the output vector to the number of output values
      // that the user told us
      output.resize (cast_int<unsigned int>
                     (this->_system_vars.size()));


      {
        const unsigned int dim = element->dim();


        /*
         * Get local coordinates to feed these into compute_data().
         * Note that the fe_type can safely be used from the 0-variable,
         * since the inverse mapping is the same for all FEFamilies
         */
        const Point mapped_point (FEMap::inverse_map (dim, element,
                                                      p));

        // loop over all vars
        for (auto index : index_range(this->_system_vars))
          {
            /*
             * the data for this variable
             */
            const unsigned int var = _system_vars[index];

            if (var == libMesh::invalid_uint)
              {
                libmesh_assert (_out_of_mesh_mode &&
                                index < _out_of_mesh_value.size());
                output(index) = _out_of_mesh_value(index);
                continue;
              }

            const FEType & fe_type = this->_dof_map.variable_type(var);

            /**
             * Build an FEComputeData that contains both input and output data
             * for the specific compute_data method.
             */
            {
              FEComputeData data (this->_eqn_systems, mapped_point);

              FEInterface::compute_data (dim, fe_type, element, data);

              // where the solution values for the var-th variable are stored
              std::vector<dof_id_type> dof_indices;
              this->_dof_map.dof_indices (element, dof_indices, var);

              // interpolate the solution
              {
                Number value = 0.;

                for (auto i : index_range(dof_indices))
                  value += this->_vector(dof_indices[i]) * data.shape[i];

                output(index) = value;
              }

            }

            // next variable
          }
      }
    }
}


void MeshFunction::discontinuous_value (const Point & p,
                                        const Real time,
                                        std::map<const Elem *, DenseVector<Number>> & output)
{
  this->discontinuous_value (p, time, output, this->_subdomain_ids.get());
}



void MeshFunction::discontinuous_value (const Point & p,
                                        const Real,
                                        std::map<const Elem *, DenseVector<Number>> & output,
                                        const std::set<subdomain_id_type> * subdomain_ids)
{
  libmesh_assert (this->initialized());

  // clear the output map
  output.clear();

  // get the candidate elements
  std::set<const Elem *> candidate_element = this->find_elements(p,subdomain_ids);

  // loop through all candidates, if the set is empty this function will return an
  // empty map
  for (const auto & element : candidate_element)
    {
      const unsigned int dim = element->dim();

      // define a temporary vector to store all values
      DenseVector<Number> temp_output (cast_int<unsigned int>(this->_system_vars.size()));

      /*
       * Get local coordinates to feed these into compute_data().
       * Note that the fe_type can safely be used from the 0-variable,
       * since the inverse mapping is the same for all FEFamilies
       */
      const Point mapped_point (FEMap::inverse_map (dim, element, p));

      // loop over all vars
      for (auto index : index_range(this->_system_vars))
        {
          /*
           * the data for this variable
           */
          const unsigned int var = _system_vars[index];

          if (var == libMesh::invalid_uint)
            {
              libmesh_assert (_out_of_mesh_mode &&
                              index < _out_of_mesh_value.size());
              temp_output(index) = _out_of_mesh_value(index);
              continue;
            }

          const FEType & fe_type = this->_dof_map.variable_type(var);

          /**
           * Build an FEComputeData that contains both input and output data
           * for the specific compute_data method.
           */
          {
            FEComputeData data (this->_eqn_systems, mapped_point);

            FEInterface::compute_data (dim, fe_type, element, data);

            // where the solution values for the var-th variable are stored
            std::vector<dof_id_type> dof_indices;
            this->_dof_map.dof_indices (element, dof_indices, var);

            // interpolate the solution
            {
              Number value = 0.;

              for (auto i : index_range(dof_indices))
                value += this->_vector(dof_indices[i]) * data.shape[i];

              temp_output(index) = value;
            }

          }

          // next variable
        }

      // Insert temp_output into output
      output[element] = temp_output;
    }
}



void MeshFunction::gradient (const Point & p,
                             const Real time,
                             std::vector<Gradient> & output)
{
  this->gradient(p, time, output, this->_subdomain_ids.get());
}



void MeshFunction::gradient (const Point & p,
                             const Real,
                             std::vector<Gradient> & output,
                             const std::set<subdomain_id_type> * subdomain_ids)
{
  libmesh_assert (this->initialized());

  const Elem * element = this->find_element(p,subdomain_ids);

  if (!element)
    {
      output.resize(0);
      return;
    }
  else
    {
      // resize the output vector to the number of output values
      // that the user told us
      output.resize (this->_system_vars.size());


      {
        const unsigned int dim = element->dim();


        /*
         * Get local coordinates to feed these into compute_data().
         * Note that the fe_type can safely be used from the 0-variable,
         * since the inverse mapping is the same for all FEFamilies
         */
        const Point mapped_point (FEMap::inverse_map (dim, element,
                                                      p));

        std::vector<Point> point_list (1, mapped_point);

        // loop over all vars
        for (auto index : index_range(this->_system_vars))
          {
            /*
             * the data for this variable
             */
            const unsigned int var = _system_vars[index];

            if (var == libMesh::invalid_uint)
              {
                libmesh_assert (_out_of_mesh_mode &&
                                index < _out_of_mesh_value.size());
                output[index] = Gradient(_out_of_mesh_value(index));
                continue;
              }

            const FEType & fe_type = this->_dof_map.variable_type(var);

            // where the solution values for the var-th variable are stored
            std::vector<dof_id_type> dof_indices;
            this->_dof_map.dof_indices (element, dof_indices, var);

            // interpolate the solution
            Gradient grad(0.);
#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
            //The other algorithm works in case of finite elements as well,
            //but this one is faster.
            if (!element->infinite())
              {
#endif
                std::unique_ptr<FEBase> point_fe (FEBase::build(dim, fe_type));
                const std::vector<std::vector<RealGradient>> & dphi = point_fe->get_dphi();
                point_fe->reinit(element, &point_list);

                for (auto i : index_range(dof_indices))
                  grad.add_scaled(dphi[i][0], this->_vector(dof_indices[i]));

#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
              }
            else
              {
                /**
                 * Build an FEComputeData that contains both input and output data
                 * for the specific compute_data method.
                 */
                FEComputeData data (this->_eqn_systems, mapped_point);
                data.enable_derivative();
                FEInterface::compute_data (dim, fe_type, element, data);
                //grad [x] = data.dshape[i](v) * dv/dx  * dof_index [i]
                // sum over all indices
                for (auto i : index_range(dof_indices))
                  {
                    // local coordinates
                    for (std::size_t v=0; v<dim; v++)
                      for (std::size_t xyz=0; xyz<LIBMESH_DIM; xyz++)
                        {
                          // FIXME: this needs better syntax: It is matrix-vector multiplication.
                          grad(xyz) += data.local_transform[v][xyz]
                            * data.dshape[i](v)
                            * this->_vector(dof_indices[i]);
                        }
                  }
              }
#endif
            output[index] = grad;
          }
      }
    }
}


void MeshFunction::discontinuous_gradient (const Point & p,
                                           const Real time,
                                           std::map<const Elem *, std::vector<Gradient>> & output)
{
  this->discontinuous_gradient (p, time, output, this->_subdomain_ids.get());
}



void MeshFunction::discontinuous_gradient (const Point & p,
                                           const Real,
                                           std::map<const Elem *, std::vector<Gradient>> & output,
                                           const std::set<subdomain_id_type> * subdomain_ids)
{
  libmesh_assert (this->initialized());

  // clear the output map
  output.clear();

  // get the candidate elements
  std::set<const Elem *> candidate_element = this->find_elements(p,subdomain_ids);

  // loop through all candidates, if the set is empty this function will return an
  // empty map
  for (const auto & element : candidate_element)
    {
      const unsigned int dim = element->dim();

      // define a temporary vector to store all values
      std::vector<Gradient> temp_output (cast_int<unsigned int>(this->_system_vars.size()));

      /*
       * Get local coordinates to feed these into compute_data().
       * Note that the fe_type can safely be used from the 0-variable,
       * since the inverse mapping is the same for all FEFamilies
       */
      const Point mapped_point (FEMap::inverse_map (dim, element, p));


      // loop over all vars
      std::vector<Point> point_list (1, mapped_point);
      for (auto index : index_range(this->_system_vars))
        {
          /*
           * the data for this variable
           */
          const unsigned int var = _system_vars[index];

          if (var == libMesh::invalid_uint)
            {
              libmesh_assert (_out_of_mesh_mode &&
                              index < _out_of_mesh_value.size());
              temp_output[index] = Gradient(_out_of_mesh_value(index));
              continue;
            }

          const FEType & fe_type = this->_dof_map.variable_type(var);

          // where the solution values for the var-th variable are stored
          std::vector<dof_id_type> dof_indices;
          this->_dof_map.dof_indices (element, dof_indices, var);

          Gradient grad(0.);

          // for performance-reasons, we use different algorithms now.
          // TODO: Check that both give the same result for finite elements.
          // Otherwive it is wrong...
#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
          if (!element->infinite())
            {
#endif
              std::unique_ptr<FEBase> point_fe (FEBase::build(dim, fe_type));
              const std::vector<std::vector<RealGradient>> & dphi = point_fe->get_dphi();
              point_fe->reinit(element, & point_list);

              for (auto i : index_range(dof_indices))
                grad.add_scaled(dphi[i][0], this->_vector(dof_indices[i]));
#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
            }
          else
            {
              /**
               * Build an FEComputeData that contains both input and output data
               * for the specific compute_data method.
               */
              //TODO: enable this for a vector of points as well...
              FEComputeData data (this->_eqn_systems, mapped_point);
              data.enable_derivative();
              FEInterface::compute_data (dim, fe_type, element, data);

              //grad [x] = data.dshape[i](v) * dv/dx  * dof_index [i]
              // sum over all indices
              for (auto i : index_range(dof_indices))
                {
                  // local coordinates.
                  for (std::size_t v=0; v<dim; v++)
                    for (std::size_t xyz=0; xyz<LIBMESH_DIM; xyz++)
                      {
                        // FIXME: this needs better syntax: It is matrix-vector multiplication.
                        grad(xyz) += data.local_transform[v][xyz]
                          * data.dshape[i](v)
                          * this->_vector(dof_indices[i]);
                      }
                }
            }
#endif
          temp_output[index] = grad;

          // next variable
        }

      // Insert temp_output into output
      output[element] = temp_output;
    }
}



#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
void MeshFunction::hessian (const Point & p,
                            const Real time,
                            std::vector<Tensor> & output)
{
  this->hessian(p, time, output, this->_subdomain_ids.get());
}



void MeshFunction::hessian (const Point & p,
                            const Real,
                            std::vector<Tensor> & output,
                            const std::set<subdomain_id_type> * subdomain_ids)
{
  libmesh_assert (this->initialized());

  const Elem * element = this->find_element(p,subdomain_ids);

  if (!element)
    {
      output.resize(0);
    }
  else
    {
#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
      if(element->infinite())
        libmesh_warning("Warning: Requested the Hessian of an Infinite element."
                        << "Second derivatives for Infinite elements"
                        << " are not yet implemented!"
                        << std::endl);
#endif
      // resize the output vector to the number of output values
      // that the user told us
      output.resize (this->_system_vars.size());


      {
        const unsigned int dim = element->dim();


        /*
         * Get local coordinates to feed these into compute_data().
         * Note that the fe_type can safely be used from the 0-variable,
         * since the inverse mapping is the same for all FEFamilies
         */
        const Point mapped_point (FEMap::inverse_map (dim, element,
                                                      p));

        std::vector<Point> point_list (1, mapped_point);

        // loop over all vars
        for (auto index : index_range(this->_system_vars))
          {
            /*
             * the data for this variable
             */
            const unsigned int var = _system_vars[index];

            if (var == libMesh::invalid_uint)
              {
                libmesh_assert (_out_of_mesh_mode &&
                                index < _out_of_mesh_value.size());
                output[index] = Tensor(_out_of_mesh_value(index));
                continue;
              }
            const FEType & fe_type = this->_dof_map.variable_type(var);

            std::unique_ptr<FEBase> point_fe (FEBase::build(dim, fe_type));
            const std::vector<std::vector<RealTensor>> & d2phi =
              point_fe->get_d2phi();
            point_fe->reinit(element, &point_list);

            // where the solution values for the var-th variable are stored
            std::vector<dof_id_type> dof_indices;
            this->_dof_map.dof_indices (element, dof_indices, var);

            // interpolate the solution
            Tensor hess;

            for (auto i : index_range(dof_indices))
              hess.add_scaled(d2phi[i][0], this->_vector(dof_indices[i]));

            output[index] = hess;
          }
      }
    }
}
#endif

const Elem * MeshFunction::find_element(const Point & p,
                                        const std::set<subdomain_id_type> * subdomain_ids) const
{
  /* Ensure that in the case of a master mesh function, the
     out-of-mesh mode is enabled either for both or for none.  This is
     important because the out-of-mesh mode is also communicated to
     the point locator.  Since this is time consuming, enable it only
     in debug mode.  */
#ifdef DEBUG
  if (this->_master != nullptr)
    {
      const MeshFunction * master =
        cast_ptr<const MeshFunction *>(this->_master);
      libmesh_error_msg_if(_out_of_mesh_mode!=master->_out_of_mesh_mode,
                           "ERROR: If you use out-of-mesh-mode in connection with master mesh "
                           "functions, you must enable out-of-mesh mode for both the master and the slave mesh function.");
    }
#endif

  // locate the point in the other mesh
  const Elem * element = (*_point_locator)(p, subdomain_ids);

  // If we have an element, but it's not a local element, then we
  // either need to have a serialized vector or we need to find a
  // local element sharing the same point.
  if (element &&
      (element->processor_id() != this->processor_id()) &&
      _vector.type() != SERIAL)
    {
      // look for a local element containing the point
      std::set<const Elem *> point_neighbors;
      element->find_point_neighbors(p, point_neighbors);
      element = nullptr;
      for (const auto & elem : point_neighbors)
        if (elem->processor_id() == this->processor_id())
          {
            element = elem;
            break;
          }
    }

  return element;
}

std::set<const Elem *> MeshFunction::find_elements(const Point & p,
                                                   const std::set<subdomain_id_type> * subdomain_ids) const
{
  /* Ensure that in the case of a master mesh function, the
     out-of-mesh mode is enabled either for both or for none.  This is
     important because the out-of-mesh mode is also communicated to
     the point locator.  Since this is time consuming, enable it only
     in debug mode.  */
#ifdef DEBUG
  if (this->_master != nullptr)
    {
      const MeshFunction * master =
        cast_ptr<const MeshFunction *>(this->_master);
      libmesh_error_msg_if(_out_of_mesh_mode!=master->_out_of_mesh_mode,
                           "ERROR: If you use out-of-mesh-mode in connection with master mesh "
                           "functions, you must enable out-of-mesh mode for both the master and the slave mesh function.");
    }
#endif

  // locate the point in the other mesh
  std::set<const Elem *> candidate_elements;
  std::set<const Elem *> final_candidate_elements;
  (*_point_locator)(p, candidate_elements, subdomain_ids);
  for (const auto & element : candidate_elements)
    {
      // If we have an element, but it's not a local element, then we
      // either need to have a serialized vector or we need to find a
      // local element sharing the same point.
      if (element &&
          (element->processor_id() != this->processor_id()) &&
          _vector.type() != SERIAL)
        {
          // look for a local element containing the point
          std::set<const Elem *> point_neighbors;
          element->find_point_neighbors(p, point_neighbors);
          for (const auto & elem : point_neighbors)
            if (elem->processor_id() == this->processor_id())
              {
                final_candidate_elements.insert(elem);
                break;
              }
        }
      else
        final_candidate_elements.insert(element);
    }

  return final_candidate_elements;
}

const PointLocatorBase & MeshFunction::get_point_locator () const
{
  libmesh_assert (this->initialized());
  return *_point_locator;
}

PointLocatorBase & MeshFunction::get_point_locator ()
{
  libmesh_assert (this->initialized());
  return *_point_locator;
}

void MeshFunction::enable_out_of_mesh_mode(const DenseVector<Number> & value)
{
  libmesh_assert (this->initialized());
  _point_locator->enable_out_of_mesh_mode();
  _out_of_mesh_mode = true;
  _out_of_mesh_value = value;
}

void MeshFunction::enable_out_of_mesh_mode(const Number & value)
{
  DenseVector<Number> v(1);
  v(0) = value;
  this->enable_out_of_mesh_mode(v);
}

void MeshFunction::disable_out_of_mesh_mode()
{
  libmesh_assert (this->initialized());
  _point_locator->disable_out_of_mesh_mode();
  _out_of_mesh_mode = false;
}

void MeshFunction::set_point_locator_tolerance(Real tol)
{
  _point_locator->set_close_to_point_tol(tol);
  _point_locator->set_contains_point_tol(tol);
}

void MeshFunction::unset_point_locator_tolerance()
{
  _point_locator->unset_close_to_point_tol();
}

void MeshFunction::set_subdomain_ids(const std::set<subdomain_id_type> * subdomain_ids)
{
  if (subdomain_ids)
    _subdomain_ids.reset();
  else
    _subdomain_ids = libmesh_make_unique<std::set<subdomain_id_type>>(*subdomain_ids);
}

} // namespace libMesh
