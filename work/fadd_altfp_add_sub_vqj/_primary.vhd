library verilog;
use verilog.vl_types.all;
entity fadd_altfp_add_sub_vqj is
    port(
        clk_en          : in     vl_logic;
        clock           : in     vl_logic;
        dataa           : in     vl_logic_vector(31 downto 0);
        datab           : in     vl_logic_vector(31 downto 0);
        result          : out    vl_logic_vector(31 downto 0)
    );
end fadd_altfp_add_sub_vqj;
