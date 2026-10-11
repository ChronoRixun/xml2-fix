#pragma once
#include "limits_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <array>
namespace gameplay_agreement_rules
{
 constexpr std::size_t request_size=0x74,reply_size=0x50,info_size=0x30,ack_size=8;
 constexpr std::uint32_t request_send=0x607d9d;
 constexpr std::uint32_t reply_broadcast=0x608477;
 constexpr std::uint32_t reply_send=0x6085f5;
 constexpr std::uint32_t info_send=0x6077c1;
 constexpr std::uint32_t ack_send=0x609a5d;
 constexpr std::uint32_t request_registration=0x6096a3;
 constexpr std::uint32_t reply_registration=0x609ee2;
 constexpr std::uint32_t info_registration=0x609cc3;
 constexpr std::uint32_t ack_registration=0x609dec;
 constexpr std::uint32_t native_direct=0x60d9c0;
 constexpr std::uint32_t native_send=0x610f30;
 constexpr std::uint32_t native_broadcast=0x611270;
 constexpr std::uint32_t native_request=0x6090d0;
 constexpr std::uint32_t native_reply=0x6079b0;
 constexpr std::uint32_t native_info=0x609970;
 constexpr std::uint32_t native_ack=0x607810;
 constexpr std::uint32_t native_join=0x609b80;
 constexpr std::uint32_t native_failure=0x609ad0;
 constexpr std::uint32_t failure_text=0x609b0e;
 constexpr std::uint32_t native_cleanup=0x6097f0;
 constexpr std::uint32_t native_dialog=0x5eb300;
 constexpr std::uint32_t native_menu=0x5d8920;
 constexpr std::uint32_t native_address=0x4bdfa0;
 constexpr std::uint32_t native_port=0x6106c0;
 constexpr std::uint32_t native_peer_disconnect=0x6106f0;
 constexpr std::uint32_t rejection_registration=0x609e3e;
 constexpr std::uint32_t join_resume=native_join+5;
 inline constexpr std::array<limits_rules::guard,16> transport_guards{{
  {request_send,"e81e5c0000","request send"},
  {reply_broadcast,"e8f48d0000","reply broadcast"},
  {reply_send,"e836890000","reply send"},
  {info_send,"e86a970000","info send"},
  {ack_send,"e8ce740000","ack send"},
  {request_registration,"68d09060006a18","request registration"},
  {reply_registration,"68b07960006a1f","reply registration"},
  {info_registration,"68709960006a1a","info registration"},
  {ack_registration,"68107860006a1b","ack registration"},
  {native_direct,"81ec18050000a1f8","native direct"},
  {native_send,"568bf18b8e1c040000","native send"},
  {native_broadcast,"558be98b851c0400","native broadcast"},
  {native_request,"558bec6aff68d0926700","native request"},
  {native_reply,"83ec14a1f8386f00538b5c24","native reply"},
  {native_info,"83ec1c53578d4c2410","native info"},
  {native_ack,"83ec2453558b6c2430","native ack"},
 }};
 inline constexpr std::array<limits_rules::guard,10> failure_guards{{
  {native_peer_disconnect,"8a442404884119c20400","native refused peer connected flag"},
  {rejection_registration,"68d09a60006a1d","native incompatibility registration"},
  {native_join,"83ec1c53555657","native join"},
  {native_failure,"56578b7c240c8b770c","native failure"},
  {failure_text,"689c466a00","failure text"},
  {native_cleanup,"51a1f8386f00535689442408","native cleanup"},
  {native_dialog,"a1ec138b0085c0750a","native dialog"},
  {native_menu,"a118ff8a0085c0750a","native menu"},
  {native_address,"8d410cc3","native address"},
  {native_port,"668b4108c3","native port"},
 }};
 template<std::size_t N> inline const limits_rules::guard* first_mismatch(const std::uint8_t* image,const std::array<limits_rules::guard,N>& guards)
 {
  for(const auto&g:guards)if(!limits_rules::matches(image+g.va-limits_rules::image_base,g.hex))return &g;
  return nullptr;
 }
}
