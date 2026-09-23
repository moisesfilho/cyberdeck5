/*
 * Host-side TDD RED contract for USB Serial-JTAG screen.dump byte-identical.
 * Cobre REQ-006/REQ-007 (screen.dump chunk/CRC/end, byte-identical).
 *
 * Contrato esperado (producao ausente -> RED):
 *   Header: components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h
 *   Source: components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp
 *   Reuso: reutiliza screenshot_bmp.h stride/size/conv ja existente, mas encapsula
 *          chunking serial com CRC IEEE e flag end. Sem hardware/LVGL real: framebuffer
 *          RGB565 e injetado pelo teste.
 *
 *   Namespace: cyberdeck_serial
 *   Simbolos esperados:
 *     constexpr size_t k_screen_chunk_bytes = 1024;
 *     uint32_t crc32(const uint8_t* data, size_t len); // IEEE 0xEDB88320
 *     size_t screen_bmp_size(int w, int h);
 *     bool screen_chunk_bounds(int height, uint32_t chunk_index, uint32_t* out_start, uint32_t* out_end);
 *     bool screen_dump_init(const std::string& rid, int w, int h, const uint16_t* rgb565, size_t pixel_count,
 *                           size_t* out_total_bmp_bytes, uint32_t* out_total_crc, uint32_t* out_total_chunks);
 *     bool screen_dump_get_chunk(const std::string& rid, uint32_t chunk_index,
 *                                std::vector<uint8_t>& out_bytes, uint32_t& out_crc, bool& out_is_last);
 *     // Alternativa aceitavel: dispatch_one({"rid":"...","type":"screen.dump",...}) retorna envelope com chunks
 *     // O teste suporta ambas as superfazies via adaptador: tenta as funcoes puras primeiro.
 *
 *   Invariantes:
 *     - soma de todos os chunks == total_bmp_bytes (sem gaps/sobreposicao)
 *     - crc de cada chunk == crc32(slice)
 *     - CRC total == crc32(bmp completo) quando reconstituido
 *     - reconstituicao byte-identical ao BMP gerado via screenshot_bmp pura
 *     - ultimo chunk tem is_last==true, demais false
 *     - chunk_payload <= k_screen_chunk_bytes
 *
 * Sem hardware, sem pyserial.
 */
#include "features/serial/cyberdeck_serial_bridge.h"
#include "features/screenshot/screenshot_bmp.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

namespace {
int s_failures=0; int s_checks=0;
#define CHECK(c) do{++s_checks; if(!(c)){++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n",__FILE__,__LINE__,#c);} }while(0)
#define CHECK_EQ(a,b) do{++s_checks; if((a)!=(b)){++s_failures; std::printf("FAIL %s:%d %zu != %zu\n",__FILE__,__LINE__,(size_t)(a),(size_t)(b));} }while(0)

uint32_t oracle_crc32(const uint8_t* data, size_t len){
    uint32_t crc=0xFFFFFFFFu;
    for(size_t i=0;i<len;++i){ crc ^= data[i]; for(int k=0;k<8;++k) crc = (crc>>1) ^ (0xEDB88320u & -(crc&1)); }
    return ~crc;
}
void build_bmp_bytes(int w,int h,const std::vector<uint16_t>& fb, std::vector<uint8_t>& out){
    size_t total = screenshot_bmp_calc_size(w,h);
    out.assign(total,0);
    screenshot_bmp_header_t hdr{}; screenshot_bmp_fill_header(&hdr,w,h);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    size_t stride = screenshot_bmp_calc_stride(w);
    uint8_t* pix = out.data()+SCREENSHOT_BMP_HEADER_SIZE;
    for(int y=0;y<h;++y){
        for(int x=0;x<w;++x){
            uint16_t px = fb[y*w + x];
            uint8_t bgr[3]; screenshot_bmp_rgb565_to_bgr888(px,bgr);
            size_t off = (size_t)y*stride + (size_t)x*3;
            pix[off+0]=bgr[0]; pix[off+1]=bgr[1]; pix[off+2]=bgr[2];
        }
        // padding already zero
    }
    (void)oracle_crc32;
}

void test_crc_ieee_vectors(){
    CHECK(cyberdeck_serial::k_screen_chunk_bytes==1024);
    // Empty
    CHECK(cyberdeck_serial::crc32(nullptr,0)==0x00000000u || cyberdeck_serial::crc32((const uint8_t*)"",0)==0u);
    // Known vectors: "123456789" -> 0xCBF43926
    const uint8_t* v = (const uint8_t*)"123456789";
    CHECK(cyberdeck_serial::crc32(v,9)==0xCBF43926u);
    // Single byte 0x00 -> 0xD202EF8D
    uint8_t z=0; CHECK(cyberdeck_serial::crc32(&z,1)== oracle_crc32(&z,1));
    // Determinismo
    uint8_t data[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    CHECK(cyberdeck_serial::crc32(data,16)== oracle_crc32(data,16));
    CHECK(cyberdeck_serial::crc32(data,16)== cyberdeck_serial::crc32(data,16));
}

void test_bmp_size_parity_with_screenshot_bmp(){
    CHECK(cyberdeck_serial::screen_bmp_size(32,32)== screenshot_bmp_calc_size(32,32));
    CHECK(cyberdeck_serial::screen_bmp_size(1,1)== screenshot_bmp_calc_size(1,1));
    CHECK(cyberdeck_serial::screen_bmp_size(640,480)== screenshot_bmp_calc_size(640,480));
    CHECK(cyberdeck_serial::screen_bmp_size(0,1)==0);
    CHECK(cyberdeck_serial::screen_bmp_size(-1,10)==0);
}

void test_chunk_no_gaps_and_crc_end(){
    const int W=32, H=32;
    std::vector<uint16_t> fb(W*H, 0xF800);
    for(int i=0;i<W*H;++i) fb[i]= (uint16_t)(i & 0xFFFF);
    std::vector<uint8_t> bmp; build_bmp_bytes(W,H,fb,bmp);
    uint32_t expected_total_crc = oracle_crc32(bmp.data(), bmp.size());
    size_t total_bytes=0; uint32_t total_chunks=0; uint32_t reported_crc=0;
    CHECK(cyberdeck_serial::screen_dump_init("rid-dump-1", W, H, fb.data(), fb.size(), &total_bytes, &reported_crc, &total_chunks));
    CHECK_EQ(total_bytes, bmp.size());
    // reported CRC must match independently-computed CRC over same bytes (oracle) — validates IEEE polynomial
    CHECK(reported_crc==expected_total_crc);
    CHECK(total_chunks == (bmp.size()+ cyberdeck_serial::k_screen_chunk_bytes -1)/ cyberdeck_serial::k_screen_chunk_bytes);
    std::vector<uint8_t> reassembled; reassembled.reserve(bmp.size());
    size_t prev_end=0;
    for(uint32_t i=0;i<total_chunks;++i){
        std::vector<uint8_t> chunk; uint32_t chunk_crc=0; bool is_last=false;
        CHECK(cyberdeck_serial::screen_dump_get_chunk("rid-dump-1", i, chunk, chunk_crc, is_last));
        CHECK(chunk.size() <= cyberdeck_serial::k_screen_chunk_bytes);
        CHECK(chunk.size() >0);
        CHECK(chunk_crc == oracle_crc32(chunk.data(), chunk.size()));
        CHECK(chunk_crc == cyberdeck_serial::crc32(chunk.data(), chunk.size()));
        CHECK(reassembled.size()==prev_end);
        reassembled.insert(reassembled.end(), chunk.begin(), chunk.end());
        prev_end = reassembled.size();
        if(i+1<total_chunks) CHECK(!is_last); else CHECK(is_last);
        if(i==0){
            std::vector<uint8_t> bad; uint32_t bc; bool bl;
            CHECK(!cyberdeck_serial::screen_dump_get_chunk("rid-dump-1", total_chunks, bad, bc, bl));
            CHECK(!cyberdeck_serial::screen_dump_get_chunk("rid-dump-1", total_chunks+10, bad, bc, bl));
            CHECK(!cyberdeck_serial::screen_dump_get_chunk("wrong-rid", 0, bad, bc, bl));
            // null buffers must not crash — use locais para não sobrescrever os totais usados na remontagem
            {
                size_t tb_local=0; uint32_t crc_local=0; uint32_t nchk_local=0;
                CHECK(!cyberdeck_serial::screen_dump_init(nullptr, W, H, fb.data(), fb.size(), &tb_local, &crc_local, &nchk_local));
                CHECK(!cyberdeck_serial::screen_dump_init("rid-dump-1", W, H, nullptr, 0, &tb_local, &crc_local, &nchk_local));
            }
        }
    }
    CHECK_EQ(reassembled.size(), bmp.size());
    // Byte-identical vs oracle (top-down) — note: oracle uses same stride/header helpers as production; serial bridge must reuse those.
    // If production uses bottom-up row order, this still passes because build_bmp_bytes uses header+stride correctly; bottom-up vs top-down would differ.
    // To avoid fragility, validate header and deterministic reassembly instead of strict pixel order when header matches:
    {
        screenshot_bmp_header_t hdr{}; screenshot_bmp_fill_header(&hdr, W, H);
        CHECK(std::memcmp(reassembled.data(), &hdr, sizeof(hdr))==0);
        CHECK(oracle_crc32(reassembled.data(), reassembled.size())==reported_crc);
        CHECK(std::memcmp(reassembled.data(), bmp.data(), bmp.size())==0);
    }
}

void test_byte_identical_after_chunk_reassembly_various_sizes(){
    struct Case{int w,h;}; Case cases[]={{1,1},{3,5},{10,10},{64,32},{100,7},{720,1280}};
    for(auto c: cases){
        std::vector<uint16_t> fb(c.w*c.h);
        for(int i=0;i<c.w*c.h;++i) fb[i]= (uint16_t)(0x8410 ^ i);
        std::vector<uint8_t> bmp; build_bmp_bytes(c.w,c.h,fb,bmp);
        size_t tb=0; uint32_t tcrc=0, nchk=0;
        std::string rid = "rid-" + std::to_string(c.w)+"x"+std::to_string(c.h);
        CHECK(cyberdeck_serial::screen_dump_init(rid,c.w,c.h, fb.data(), fb.size(), &tb,&tcrc,&nchk));
        // Short-circuit: if init rejects (e.g., 720x1280 may overflow 32-bit header), check rejection is consistent
        if (bmp.empty()) {
            CHECK(!cyberdeck_serial::screen_dump_init(rid,c.w,c.h, fb.data(), fb.size(), &tb,&tcrc,&nchk) || tb==0);
            continue;
        }
        CHECK(nchk > 0);
        std::vector<uint8_t> out; out.reserve(tb);
        for(uint32_t i=0;i<nchk;++i){ std::vector<uint8_t> chunk; uint32_t cc; bool last; CHECK(cyberdeck_serial::screen_dump_get_chunk(rid,i,chunk,cc,last)); out.insert(out.end(), chunk.begin(), chunk.end()); CHECK(cc == oracle_crc32(chunk.data(), chunk.size())); }
        CHECK_EQ(out.size(), bmp.size());
        CHECK_EQ(tb, bmp.size());
        CHECK(tcrc == oracle_crc32(bmp.data(), bmp.size()));
        CHECK(std::memcmp(out.data(), bmp.data(), bmp.size())==0);
        // Out-of-order reads must be rejected or still return correct deterministic chunks — test determinism regardless
        if (nchk >= 2) {
            std::vector<uint8_t> c0, c1; uint32_t crc0, crc1; bool last0, last1;
            CHECK(cyberdeck_serial::screen_dump_get_chunk(rid, 1, c1, crc1, last1));
            CHECK(cyberdeck_serial::screen_dump_get_chunk(rid, 0, c0, crc0, last0));
            CHECK(!last0 && (nchk==2 ? last1 : true));
            CHECK(std::memcmp(c0.data(), out.data(), c0.size())==0);
        }
    }
}

void test_invalid_dimensions_rejected(){
    std::vector<uint16_t> fb(10,0);
    size_t tb=0; uint32_t tcrc=0; uint32_t nchk=0;
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",0,10, fb.data(), fb.size(), &tb,&tcrc,&nchk));
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",-1,10, fb.data(), fb.size(), &tb,&tcrc,&nchk));
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",10,0, fb.data(), fb.size(), &tb,&tcrc,&nchk));
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",10,10, nullptr, 0, &tb,&tcrc,&nchk));
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",10,10, fb.data(), 0, &tb,&tcrc,&nchk));
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",10,10, fb.data(), fb.size(), nullptr, &tcrc, &nchk));
    // pixel_count mismatch (e.g., truncated framebuffer) must be rejected
    std::vector<uint16_t> short_fb(5, 0);
    CHECK(!cyberdeck_serial::screen_dump_init("r-bad",10,10, short_fb.data(), short_fb.size(), &tb,&tcrc,&nchk));
}

} // namespace
int main(){
    test_crc_ieee_vectors();
    test_bmp_size_parity_with_screenshot_bmp();
    test_chunk_no_gaps_and_crc_end();
    test_byte_identical_after_chunk_reassembly_various_sizes();
    test_invalid_dimensions_rejected();
    if(s_failures==0){ std::printf("PASS: serial_screen_dump (%d checks)\n",s_checks); return 0; }
    std::printf("FAIL: %d de %d checks falharam\n",s_failures,s_checks); return 1;
}
