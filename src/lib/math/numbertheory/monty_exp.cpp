/*
* Montgomery Exponentiation
* (C) 1999-2010,2012,2018 Jack Lloyd
*     2016 Matthias Gierlings
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include <botan/internal/monty_exp.h>
#include <botan/internal/ct_utils.h>
#include <botan/internal/rounding.h>
#include <botan/numthry.h>
#include <botan/reducer.h>
#include <botan/monty.h>

namespace Botan {

class Montgomery_Exponentation_State
   {
   public:
      Montgomery_Exponentation_State(std::shared_ptr<const Montgomery_Params> params,
                                     const BigInt& g,
                                     size_t window_bits);

      BigInt exponentiation(const BigInt& k) const;
   private:
      std::shared_ptr<const Montgomery_Params> m_params;
      std::vector<Montgomery_Int> m_g;
      size_t m_window_bits;
   };

Montgomery_Exponentation_State::Montgomery_Exponentation_State(std::shared_ptr<const Montgomery_Params> params,
                                                               const BigInt& g,
                                                               size_t window_bits) :
   m_params(params),
   m_window_bits(window_bits == 0 ? 4 : window_bits)
   {
   if(m_window_bits < 1 || m_window_bits > 12) // really even 8 is too large ...
      throw Invalid_Argument("Invalid window bits for Montgomery exponentiation");

   const size_t window_size = (1U << m_window_bits);

   m_g.reserve(window_size);

   m_g.push_back(Montgomery_Int(m_params, m_params->R1(), false));;

   m_g.push_back(Montgomery_Int(m_params, g));

   const Montgomery_Int& monty_g = m_g[1];

   for(size_t i = 2; i != window_size; ++i)
      {
      m_g.push_back(monty_g * m_g[i - 1]);
      }

   // Resize each element to exactly p words
   for(size_t i = 0; i != window_size; ++i)
      {
      m_g[i].fix_size();
      }
   }

namespace {

void const_time_lookup(secure_vector<word>& output,
                        const std::vector<Montgomery_Int>& g,
                        size_t nibble)
   {
   const size_t words = output.size();

   clear_mem(output.data(), output.size());

   for(size_t i = 0; i != g.size(); ++i)
      {
      const secure_vector<word>& vec = g[i].repr().get_word_vector();

      BOTAN_ASSERT(vec.size() >= words,
                   "Word size as expected in const_time_lookup");

      const word mask = CT::is_equal<word>(i, nibble);

      for(size_t w = 0; w != words; ++w)
         output[w] |= (mask & vec[w]);
      }
   }

}

BigInt Montgomery_Exponentation_State::exponentiation(const BigInt& scalar) const
   {
   const size_t exp_nibbles = (scalar.bits() + m_window_bits - 1) / m_window_bits;

   Montgomery_Int x(m_params, m_params->R1(), false);

   secure_vector<word> e_bits(m_params->p_words());
   secure_vector<word> ws;

   for(size_t i = exp_nibbles; i > 0; --i)
      {
      for(size_t j = 0; j != m_window_bits; ++j)
         {
         x.square_this(ws);
         }

      const uint32_t nibble = scalar.get_substring(m_window_bits*(i-1), m_window_bits);

      const_time_lookup(e_bits, m_g, nibble);

      x.mul_by(e_bits, ws);
      }

   return x.value();
   }

std::shared_ptr<const Montgomery_Exponentation_State>
monty_precompute(std::shared_ptr<const Montgomery_Params> params,
                 const BigInt& g,
                 size_t window_bits)
   {
   return std::make_shared<const Montgomery_Exponentation_State>(params, g, window_bits);
   }

BigInt monty_execute(const Montgomery_Exponentation_State& precomputed_state,
                     const BigInt& k)
   {
   return precomputed_state.exponentiation(k);
   }

BigInt monty_multi_exp(std::shared_ptr<const Montgomery_Params> params_p,
                       const BigInt& x_bn,
                       const BigInt& z1,
                       const BigInt& y_bn,
                       const BigInt& z2)
   {
   if(z1.is_negative() || z2.is_negative())
      throw Invalid_Argument("multi_exponentiate exponents must be positive");

   const size_t z_bits = round_up(std::max(z1.bits(), z2.bits()), 2);

   secure_vector<word> ws;

   const Montgomery_Int one(params_p, params_p->R1(), false);
   //const Montgomery_Int one(params_p, 1);

   const Montgomery_Int x1(params_p, x_bn);
   const Montgomery_Int x2 = x1.square(ws);
   const Montgomery_Int x3 = x2.mul(x1, ws);

   const Montgomery_Int y1(params_p, y_bn);
   const Montgomery_Int y2 = y1.square(ws);
   const Montgomery_Int y3 = y2.mul(y1, ws);

   const Montgomery_Int y1x1 = y1.mul(x1, ws);
   const Montgomery_Int y1x2 = y1.mul(x2, ws);
   const Montgomery_Int y1x3 = y1.mul(x3, ws);

   const Montgomery_Int y2x1 = y2.mul(x1, ws);
   const Montgomery_Int y2x2 = y2.mul(x2, ws);
   const Montgomery_Int y2x3 = y2.mul(x3, ws);

   const Montgomery_Int y3x1 = y3.mul(x1, ws);
   const Montgomery_Int y3x2 = y3.mul(x2, ws);
   const Montgomery_Int y3x3 = y3.mul(x3, ws);

   const Montgomery_Int* M[16] = {
      &one,
      &x1,                    // 0001
      &x2,                    // 0010
      &x3,                    // 0011
      &y1,                    // 0100
      &y1x1,
      &y1x2,
      &y1x3,
      &y2,                    // 1000
      &y2x1,
      &y2x2,
      &y2x3,
      &y3,                    // 1100
      &y3x1,
      &y3x2,
      &y3x3
   };

   Montgomery_Int H = one;

   for(size_t i = 0; i != z_bits; i += 2)
      {
      if(i > 0)
         {
         H.square_this(ws);
         H.square_this(ws);
         }

      const uint8_t z1_b = z1.get_substring(z_bits - i - 2, 2);
      const uint8_t z2_b = z2.get_substring(z_bits - i - 2, 2);

      const uint8_t z12 = (4*z2_b) + z1_b;

      H.mul_by(*M[z12], ws);
      }

   return H.value();
   }

}
