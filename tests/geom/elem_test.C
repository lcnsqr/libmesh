#include "test_comm.h"

#include <libmesh/elem.h>
#include <libmesh/enum_elem_type.h>
#include <libmesh/mesh.h>
#include <libmesh/mesh_generation.h>

#include "libmesh_cppunit.h"

using namespace libMesh;

template <ElemType elem_type>
class ElemTest : public CppUnit::TestCase {

private:
  Mesh * _mesh;

public:
  void setUp()
  {
    const Real minpos = 1.5, maxpos = 5.5;
    const unsigned int N = 2;

    _mesh = new Mesh(*TestCommWorld);
    const std::unique_ptr<Elem> test_elem = Elem::build(elem_type);
    const unsigned int dim = test_elem->dim();
    const unsigned int use_y = dim > 1;
    const unsigned int use_z = dim > 2;

    MeshTools::Generation::build_cube (*_mesh,
                                       N, N*use_y, N*use_z,
                                       minpos, maxpos,
                                       minpos, use_y*maxpos,
                                       minpos, use_z*maxpos,
                                       elem_type);
  }

  void tearDown()
  {
    delete _mesh;
  }

  void test_bounding_box()
  {
    for (const auto & elem : _mesh->active_local_element_ptr_range())
      {
        const BoundingBox bbox = elem->loose_bounding_box();

        // The "loose" bounding box should actually be pretty tight
        // in most of these cases, but for weirdly aligned triangles
        // (such as occur in pyramid elements) it won't be, so we'll
        // just test against a widened bounding box.
        BoundingBox wide_bbox(elem->point(0), elem->point(0));

        for (unsigned int n = 0; n != elem->n_nodes(); ++n)
          {
            const Point & p = elem->point(n);

            CPPUNIT_ASSERT(bbox.contains_point(p));

            wide_bbox.union_with
              (BoundingBox(elem->point(n), elem->point(n)));
          }

        wide_bbox.scale(1. / 3.);

        CPPUNIT_ASSERT(!bbox.contains_point(wide_bbox.min()));
        CPPUNIT_ASSERT(!bbox.contains_point(wide_bbox.max()));
      }
  }

  void test_maps()
  {
    for (const auto & elem : _mesh->active_local_element_ptr_range())
      {
        for (const auto edge : elem->edge_index_range())
          for (const auto side_on_edge : elem->sides_on_edge(edge))
            for (const auto node_on_edge : elem->nodes_on_edge(edge))
              CPPUNIT_ASSERT(elem->is_node_on_side(node_on_edge, side_on_edge));

        for (const auto side : elem->side_index_range())
          for (const auto node_on_side : elem->nodes_on_side(side))
            CPPUNIT_ASSERT(elem->is_node_on_side(node_on_side, side));

        for (const auto edge : elem->edge_index_range())
          for (const auto node_on_edge : elem->nodes_on_edge(edge))
            CPPUNIT_ASSERT(elem->is_node_on_edge(node_on_edge, edge));

        for (const auto edge : elem->edge_index_range())
          for (const auto side_on_edge : elem->sides_on_edge(edge))
            CPPUNIT_ASSERT(elem->is_edge_on_side(edge, side_on_edge));
      }
  }

  void test_contains_point_node()
  {
    for (const auto & elem : _mesh->active_local_element_ptr_range())
      for (const auto n : elem->node_index_range())
#ifndef LIBMESH_ENABLE_EXCEPTIONS
        // If this node has a singular Jacobian, we need exceptions in order
        // to catch the failed inverse_map solve and return the singular
        // master point. Therefore, if we don't have exceptions and we're
        // at a singular node, we can't test this. As of the writing of
        // this comment, this issue exists for only Pyramid elements at
        // the apex.
        if (elem->local_singular_node(elem->point(n), TOLERANCE*TOLERANCE) == invalid_uint)
#endif
          CPPUNIT_ASSERT(elem->contains_point(elem->point(n)));
  }

  void test_permute()
  {
    for (const auto & elem : _mesh->active_local_element_ptr_range())
      for (const auto p : IntRange<unsigned int>(0, elem->n_permutations()))
        {
          elem->permute(p);
          CPPUNIT_ASSERT(elem->has_invertible_map());
        }
  }

  void test_center_node_on_side()
  {
    for (const auto & elem : _mesh->active_local_element_ptr_range())
      for (const auto s : elem->side_index_range())
        {
          if (elem->type() == EDGE2 || elem->type() == EDGE3 || elem->type() == EDGE4)
            CPPUNIT_ASSERT_EQUAL((unsigned int)s, elem->center_node_on_side(s));
          else if (elem->type() == TRI6 || elem->type() == TRI7)
            CPPUNIT_ASSERT_EQUAL((unsigned int)(s + 3), elem->center_node_on_side(s));
          else if (elem->type() == QUAD8 || elem->type() == QUAD9 || elem->type() == QUADSHELL8)
            CPPUNIT_ASSERT_EQUAL((unsigned int)(s + 4), elem->center_node_on_side(s));
          else if (elem->type() == HEX27)
            CPPUNIT_ASSERT_EQUAL((unsigned int)(s + 20), elem->center_node_on_side(s));
          else if (elem->type() == PRISM18 && s >= 1 && s <= 3)
            CPPUNIT_ASSERT_EQUAL((unsigned int)(s + 14), elem->center_node_on_side(s));
          else if (elem->type() == PYRAMID14 && s == 4)
            CPPUNIT_ASSERT_EQUAL((unsigned int)(13), elem->center_node_on_side(s));
          else
            CPPUNIT_ASSERT_EQUAL(invalid_uint, elem->center_node_on_side(s));
        }
  }
};

#define ELEMTEST                                \
  CPPUNIT_TEST( test_bounding_box );            \
  CPPUNIT_TEST( test_maps );                    \
  CPPUNIT_TEST( test_permute );                 \
  CPPUNIT_TEST( test_contains_point_node );     \
  CPPUNIT_TEST( test_center_node_on_side );

#define INSTANTIATE_ELEMTEST(elemtype)                          \
  class ElemTest_##elemtype : public ElemTest<elemtype> {       \
  public:                                                       \
  CPPUNIT_TEST_SUITE( ElemTest_##elemtype );                    \
  ELEMTEST;                                                     \
  CPPUNIT_TEST_SUITE_END();                                     \
  };                                                            \
                                                                \
  CPPUNIT_TEST_SUITE_REGISTRATION( ElemTest_##elemtype )

INSTANTIATE_ELEMTEST(EDGE2);
INSTANTIATE_ELEMTEST(EDGE3);
INSTANTIATE_ELEMTEST(EDGE4);

#if LIBMESH_DIM > 1
INSTANTIATE_ELEMTEST(TRI3);
INSTANTIATE_ELEMTEST(TRI6);
INSTANTIATE_ELEMTEST(TRI7);

INSTANTIATE_ELEMTEST(QUAD4);
INSTANTIATE_ELEMTEST(QUAD8);
INSTANTIATE_ELEMTEST(QUAD9);
#endif

#if LIBMESH_DIM > 2
INSTANTIATE_ELEMTEST(TET4);
INSTANTIATE_ELEMTEST(TET10);
INSTANTIATE_ELEMTEST(TET14);

INSTANTIATE_ELEMTEST(HEX8);
INSTANTIATE_ELEMTEST(HEX20);
INSTANTIATE_ELEMTEST(HEX27);

INSTANTIATE_ELEMTEST(PRISM6);
INSTANTIATE_ELEMTEST(PRISM15);
INSTANTIATE_ELEMTEST(PRISM18);

INSTANTIATE_ELEMTEST(PYRAMID5);
INSTANTIATE_ELEMTEST(PYRAMID13);
INSTANTIATE_ELEMTEST(PYRAMID14);
#endif
