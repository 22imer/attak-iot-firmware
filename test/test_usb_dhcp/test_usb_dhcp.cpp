#include <unity.h>
#include <array>
#include <cstring>
#include "core/usb_dhcp.h"

void setUp() {}
void tearDown() {}
namespace {
constexpr uint8_t deviceMac[] = {2, 1, 2, 3, 4, 5};
constexpr uint8_t peerMac[] = {2, 6, 7, 8, 9, 10};
using Frame = std::array<uint8_t, 342>;
void put16(uint8_t *p, unsigned n) { p[0] = n >> 8; p[1] = n; }
Frame request(uint8_t type) {
    Frame f{};
    memset(f.data(), 0xff, 6);
    memcpy(f.data() + 6, peerMac, 6);
    f[12] = 8;
    f[14] = 0x45; put16(f.data()+16, 328); f[23] = 17;
    put16(f.data()+34, 68); put16(f.data()+36, 67); put16(f.data()+38, 308);
    uint8_t *b = f.data()+42;
    b[0]=1; b[1]=1; b[2]=6;
    b[4]=0x12; b[5]=0x34; b[6]=0x56; b[7]=0x78;
    memcpy(b+28, peerMac, 6);
    b[236]=99; b[237]=130; b[238]=83; b[239]=99;
    b[240]=53; b[241]=1; b[242]=type; b[243]=255;
    return f;
}
void selectLease(Frame &f, uint8_t last, uint8_t serverLast=1) {
    const uint8_t options[] = {53,1,3,50,4,192,168,7,last,54,4,192,168,7,serverLast,255};
    memcpy(f.data()+282, options, sizeof(options));
}
const uint8_t *option(const Frame &f, uint8_t code) {
    size_t pos=282;
    while(pos < f.size()) {
        const uint8_t id=f[pos++];
        if(id==255) break;
        if(id==0) continue;
        if(pos>=f.size()) return nullptr;
        const uint8_t n=f[pos++];
        if(pos+n>f.size()) return nullptr;
        if(id==code) return f.data()+pos;
        pos+=n;
    }
    return nullptr;
}
}
void test_discover_offers_usb_lease_without_internet_routes() {
    Frame in=request(1), out{};
    const auto r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled);
    TEST_ASSERT_EQUAL_UINT32(342,r.size);
    const uint8_t broadcast[]={255,255,255,255,255,255};
    const uint8_t lease[]={192,168,7,2};
    const uint8_t server[]={192,168,7,1};
    const uint8_t mask[]={255,255,255,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(broadcast,out.data(),6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(deviceMac,out.data()+6,6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.data()+46,out.data()+46,4); // transaction ID
    TEST_ASSERT_EQUAL_UINT8_ARRAY(peerMac,out.data()+70,6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(lease,out.data()+58,4);
    TEST_ASSERT_NOT_NULL(option(out,53));
    TEST_ASSERT_EQUAL_UINT8(2,*option(out,53)); // DHCPOFFER
    TEST_ASSERT_EQUAL_UINT8_ARRAY(server,option(out,54),4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mask,option(out,1),4);
    TEST_ASSERT_NULL(option(out,3)); // no router
    TEST_ASSERT_NULL(option(out,6)); // no DNS
    unsigned sum=0;
    for(size_t i=14;i<34;i+=2) sum+=(out[i]<<8)|out[i+1];
    while(sum>>16) sum=(sum&65535)+(sum>>16);
    TEST_ASSERT_EQUAL_UINT32(65535,sum); // valid IPv4 header checksum
}
void test_request_acks_selected_and_renewing_usb_lease() {
    Frame in=request(3), out{};
    selectLease(in,2);
    auto r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled && r.size>0);
    TEST_ASSERT_EQUAL_UINT8(5,*option(out,53)); // DHCPACK
    in=request(3);
    const uint8_t lease[]={192,168,7,2};
    memcpy(in.data()+54,lease,4); // ciaddr, no requested-IP or server-ID options
    r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled && r.size>0);
    TEST_ASSERT_EQUAL_UINT8(5,*option(out,53));
}
void test_request_for_wrong_usb_address_is_naked() {
    Frame in=request(3),out{};
    selectLease(in,3);
    const auto r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled && r.size>0);
    TEST_ASSERT_EQUAL_UINT8(6,*option(out,53)); // DHCPNAK
    const uint8_t zero[]={0,0,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero,out.data()+58,4);
}
void test_other_server_request_is_not_answered() {
    Frame in=request(3),out{};
    selectLease(in,2,99);
    const auto r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled);
    TEST_ASSERT_EQUAL_UINT32(0,r.size);
}
void test_malformed_dhcp_and_small_output_are_consumed_safely() {
    Frame in=request(1),out{};
    in[283]=255; // option exceeds UDP payload
    auto r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled);
    TEST_ASSERT_EQUAL_UINT32(0,r.size);
    in=request(1); in[278]=0; // invalid magic cookie
    r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_TRUE(r.handled);
    TEST_ASSERT_EQUAL_UINT32(0,r.size);
    in=request(1);
    out.fill(0x5a);
    r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),341);
    TEST_ASSERT_TRUE(r.handled);
    TEST_ASSERT_EQUAL_UINT32(0,r.size);
    TEST_ASSERT_EQUAL_UINT8(0x5a,out.back());
    in[12]=0x86;in[13]=0xdd; // IPv6 is not stolen from lwIP
    r=usbDhcp::respond(in.data(),in.size(),deviceMac,out.data(),out.size());
    TEST_ASSERT_FALSE(r.handled);
}
int main(int,char **) {
    UNITY_BEGIN();
    RUN_TEST(test_discover_offers_usb_lease_without_internet_routes);
    RUN_TEST(test_request_acks_selected_and_renewing_usb_lease);
    RUN_TEST(test_request_for_wrong_usb_address_is_naked);
    RUN_TEST(test_other_server_request_is_not_answered);
    RUN_TEST(test_malformed_dhcp_and_small_output_are_consumed_safely);
    return UNITY_END();
}
