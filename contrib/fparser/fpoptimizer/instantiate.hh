/* BEGIN_EXPLICIT_INSTANTATION */

#ifndef FP_DISABLE_DOUBLE_TYPE
# define FUNCTIONPARSER_INSTANTIATE_D(g) g(double)
#else
# define FUNCTIONPARSER_INSTANTIATE_D(g)
#endif

#ifdef FP_SUPPORT_FLOAT_TYPE
# define FUNCTIONPARSER_INSTANTIATE_F(g) g(float)
#else
# define FUNCTIONPARSER_INSTANTIATE_F(g)
#endif

#ifdef FP_SUPPORT_LONG_DOUBLE_TYPE
# define FUNCTIONPARSER_INSTANTIATE_LD(g) g(long double)
#else
# define FUNCTIONPARSER_INSTANTIATE_LD(g)
#endif

#ifdef FP_SUPPORT_LONG_INT_TYPE
# define FUNCTIONPARSER_INSTANTIATE_LI(g) g(long)
#else
# define FUNCTIONPARSER_INSTANTIATE_LI(g)
#endif

/* Call the given instantiater for each type supported in FPoptimizer */
#define FPOPTIMIZER_EXPLICITLY_INSTANTIATE(generator) \
  FUNCTIONPARSER_INSTANTIATE_D(generator) \
  FUNCTIONPARSER_INSTANTIATE_F(generator) \
  FUNCTIONPARSER_INSTANTIATE_LD(generator) \
  FUNCTIONPARSER_INSTANTIATE_LI(generator) \
  /*FUNCTIONPARSER_INSTANTIATE_MF(generator)*/ \
  /*FUNCTIONPARSER_INSTANTIATE_GI(generator)*/ \
  /*FUNCTIONPARSER_INSTANTIATE_CD(generator)*/ \
  /*FUNCTIONPARSER_INSTANTIATE_CF(generator)*/ \
  /*FUNCTIONPARSER_INSTANTIATE_CLD(generator)*/

/* END_EXPLICIT_INSTANTATION */
