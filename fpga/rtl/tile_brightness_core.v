// Tile brightness equalizer for the Kria KV260 programmable logic.
// Clock: 100 MHz.
//
// Ubuntu writes one grayscale tile, sets target (the frame mean), pulses start,
// then reads the scaled tile and `cycles`.
//
//   P cycles   add every pixel into sum
//   24 cycles  gain_q8 = (target << 8) / max(mean, 1)
//   P cycles   pixel = min(255, (pixel * gain_q8 + 128) >> 8)
//   2 cycles   start and done
// cycles per tile = 2*P + 26

module tile_brightness_core #(
    parameter integer MAX_PIXELS = 4096
) (
    input  wire                          clk,
    input  wire                          rst_n,
    input  wire                          start,
    input  wire [15:0]                   pixel_count,
    input  wire [7:0]                    target,
    input  wire                          wr_en,
    input  wire [$clog2(MAX_PIXELS)-1:0] wr_addr,
    input  wire [7:0]                    wr_data,
    input  wire [$clog2(MAX_PIXELS)-1:0] rd_addr,
    output wire [7:0]                    rd_data,
    output reg                           busy,
    output reg                           done,
    output reg  [31:0]                   cycles
);

    localparam integer ADDR_W = $clog2(MAX_PIXELS);
    localparam [2:0] S_IDLE  = 3'd0;
    localparam [2:0] S_SUM   = 3'd1;
    localparam [2:0] S_DIV   = 3'd2;
    localparam [2:0] S_SCALE = 3'd3;
    localparam [2:0] S_DONE  = 3'd4;

    reg [7:0]  mem [0:MAX_PIXELS-1];
    reg [2:0]  state;
    reg [31:0] sum;
    reg [15:0] index;
    reg [15:0] count;
    reg [7:0]  target_r;
    reg [31:0] div_den;
    reg [31:0] div_num;
    reg [15:0] gain_q8;
    reg [4:0]  div_left;

    assign rd_data = mem[rd_addr];

    wire [31:0] tile_mean = (sum < count || count == 16'd0) ? 32'd1 :
                            ((sum / count) == 0 ? 32'd1 :
                            ((sum / count) > 32'd255 ? 32'd255 : (sum / count)));

    always @(posedge clk) begin
        if (wr_en && state == S_IDLE) begin
            mem[wr_addr] <= wr_data;
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= S_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            cycles <= 32'd0;
            sum <= 32'd0;
            index <= 16'd0;
            count <= 16'd0;
            target_r <= 8'd1;
            div_den <= 32'd1;
            div_num <= 32'd0;
            gain_q8 <= 16'd256;
            div_left <= 5'd0;
        end else begin
            done <= 1'b0;
            case (state)
                S_IDLE: begin
                    busy <= 1'b0;
                    if (start && pixel_count != 16'd0 && pixel_count <= MAX_PIXELS[15:0]) begin
                        busy <= 1'b1;
                        cycles <= 32'd1;
                        sum <= 32'd0;
                        index <= 16'd0;
                        count <= pixel_count;
                        target_r <= (target == 8'd0) ? 8'd1 : target;
                        state <= S_SUM;
                    end
                end

                S_SUM: begin
                    cycles <= cycles + 32'd1;
                    sum <= sum + {24'd0, mem[index[ADDR_W-1:0]]};
                    if (index + 16'd1 == count) begin
                        div_left <= 5'd24;
                        gain_q8 <= 16'd0;
                        state <= S_DIV;
                    end else begin
                        index <= index + 16'd1;
                    end
                end

                S_DIV: begin
                    cycles <= cycles + 32'd1;
                    if (div_left == 5'd24) begin
                        div_den <= tile_mean;
                        div_num <= {16'd0, target_r, 8'd0};
                        div_left <= 5'd23;
                    end else begin
                        if ({div_num[30:0], 1'b0} >= div_den) begin
                            div_num <= {div_num[30:0], 1'b0} - div_den;
                            gain_q8 <= {gain_q8[14:0], 1'b1};
                        end else begin
                            div_num <= {div_num[30:0], 1'b0};
                            gain_q8 <= {gain_q8[14:0], 1'b0};
                        end
                        if (div_left == 5'd0) begin
                            index <= 16'd0;
                            state <= S_SCALE;
                        end else begin
                            div_left <= div_left - 5'd1;
                        end
                    end
                end

                S_SCALE: begin
                    cycles <= cycles + 32'd1;
                    begin : scale_px
                        reg [23:0] product;
                        product = (mem[index[ADDR_W-1:0]] * gain_q8) + 16'd128;
                        mem[index[ADDR_W-1:0]] <= product[15:8];
                    end
                    if (index + 16'd1 == count) begin
                        state <= S_DONE;
                    end else begin
                        index <= index + 16'd1;
                    end
                end

                S_DONE: begin
                    cycles <= cycles + 32'd1;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
