#!/usr/bin/env php
<?php

set_error_handler(function($severity, $message, $file, $line) {
	if (!(error_reporting() & $severity))
		return;

	throw new ErrorException($message, 0, $severity, $file, $line);
});

class ini_file
{
	private $_ini;

	public function __construct($path)
	{
		$this->_ini = parse_ini_file($path, false, INI_SCANNER_RAW);
	}

	public function __get($name)
	{
		if (array_key_exists($name, $this->_ini))
			return trim($this->_ini[$name]);

		return '';
	}

	public function get_array($name)
	{
		return preg_split('#\s+#', $this->{$name}, -1, PREG_SPLIT_NO_EMPTY);
	}
}

class ninja_rule
{
	public $name;
	public $vars = [];

	public function __construct($name, $cmd, $vars = [])
	{
		$this->name = $name;
		$this->vars = $vars;
		$this->vars['command'] = $cmd;
	}

	public function set_var($name, $val)
	{
		$this->vars[$name] = $val;
		return $this;
	}

	public function gen()
	{
		$s = "rule $this->name\n";

		foreach ($this->vars as $k => $v) {
			$s.= "  $k = $v\n";
		}

		return $s;
	}
}

class ninja_build
{
	public $rule;
	public $outputs = [];
	public $implicit_outputs = [];
	public $inputs = [];
	public $implicit_inputs = [];
	public $order_only = [];
	public $vars = [];

	public function __construct($rule, $out = [], $in = [], $implicit = [])
	{
		$this->rule = $rule;
		$this->outputs = $out;
		$this->inputs = $in;
		$this->implicit_inputs = $implicit;
	}

	public function escape_path($path)
	{
		return str_replace(['$ ', ' ', ':'], ['$$ ', '$ ', '$:'], $path);
	}

	public function _combine(&$arr, $v, $delim = null)
	{
		$list = null;
		if (is_array($v) && count($v) > 0)
			$list = $v;
		else if (is_string($v))
			$list = [$v];

		if ($list) {
			if ($delim)
				$arr[] = $delim;

			foreach ($list as $i)
				$arr[] = $this->escape_path($i);
		}
	}

	public function set_var($name, $val)
	{
		$this->vars[$name] = $val;
		return $this;
	}

	public function gen()
	{
		$in = [];
		$out = [];

		$this->_combine($in, $this->inputs);
		$this->_combine($in, $this->implicit_inputs, '|');
		$this->_combine($in, $this->order_only, '||');

		$this->_combine($out, $this->outputs);
		$this->_combine($out, $this->implicit_outputs, '|');

		$s = sprintf("build %s: %s %s\n", implode(' ', $out), $this->rule, implode(' ', $in));

		foreach ($this->vars as $k => $v) {
			if ($v !== null)
				$s.= "  $k = $v\n";
		}

		return $s;
	}
}

class ninja
{
        public $version = '1.7';
	public $globals = [];
	public $rules = [];
	public $builds = [];
	public $default;

	static public function escape($str)
	{
		return str_replace('$', '$$', $str);
	}

	public function add_rule($name, $cmd, $vars = [])
	{
		return $this->rules[$name] = new ninja_rule($name, $cmd, $vars);
	}

	public function add_build($rule, $out = [], $in = [], $implicit = [])
	{
		return $this->builds[] = new ninja_build($rule, $out, $in, $implicit);
	}

	public function set_global($name, $val)
	{
		$this->globals[$name] = $val;
	}

	public function gen()
	{
		$s = "# This file is generated, please don't edit by hand\n\n";
		$s.= "ninja_required_version = $this->version\n\n";

		foreach ($this->globals as $k => $v)
			$s.= $k . ' = ' . $v . "\n";

		$s.= "\n";

		foreach ($this->rules as $r)
			$s.= $r->gen() . "\n";

		foreach ($this->builds as $b)
			$s.= $b->gen() . "\n";

		if ($this->default)
			$s.= "default $this->default\n";

		return $s;
	}

	public function write($path)
	{
		file_put_contents($path, $this->gen());
	}
}

function flat($a)
{
	$ret = [];

	array_walk_recursive($a, function($i) use(&$ret) {
		$ret[] = $i;
	});

	return $ret;
}

function setup_gnu_toolchain($arch, $name = '')
{
	global $n;

	$suffix = $name ? '_' . $name : '';

	$n->add_rule('cc' . $suffix, $arch . 'gcc -MMD -MT $out -MF $out.d $cflags -c $in -o $out')
		->set_var('description', 'CC $out')
		->set_var('depfile', '$out.d')
		->set_var('deps', 'gcc');

	$n->add_rule('cxx' . $suffix, $arch . 'g++ -MMD -MT $out -MF $out.d $cxxflags -c $in -o $out')
		->set_var('description', 'CXX $out')
		->set_var('depfile', '$out.d')
		->set_var('deps', 'gcc');

	$n->add_rule('as' . $suffix, $arch . 'as -o $out $in')
		->set_var('description', 'AS $out');

	$n->add_rule('link' . $suffix, $arch . 'gcc -o $out $cflags $ldflags -Wl,--start-group $in $libs -Wl,--end-group')
		->set_var('description', 'LINK $out');

	$n->add_rule('size' . $suffix, $arch . 'size $in > $out')
		->set_var('description', 'SIZE $out');

	$n->add_rule('elf2bin' . $suffix, $arch . 'objcopy -O binary $in $out')
		->set_var('description', 'OBJCOPY $out');

	$n->add_rule('elf2ihex' . $suffix, $arch . 'objcopy $opts -O ihex $in $out')
		->set_var('description', 'OBJCOPY $out');

	$n->add_rule('elf2as' . $suffix, $arch . 'objdump -D $in > $out')
		->set_var('description', 'OBJDUMP $out');
}

function add_cc_link_build($config)
{
	global $n;

	$toolchain = $config['toolchain'] ?? '';
	$suffix = $toolchain ? '_' . $toolchain : '';

	$output = $config['output'] ?? null;
	$sources = $config['sources'] ?? null;
	$link_deps = $config['link_deps'] ?? [];
	$cflags = $config['cflags'] ?? '';
	$cxxflags = $config['cxxflags'] ?? '';
	$ldflags = $config['ldflags'] ?? '';
	$libs = $config['libs'] ?? '';
	$obj_ext = $config['obj_ext'] ?? '.o';
	$dep_path = $config['dep_path'] ?? null;
	$obj_deps_map = $config['obj_deps'] ?? [];

	$opts_ns = $config['name'] ?? null;
	if ($opts_ns) {
		$n->set_global("cflags_$opts_ns", $cflags);
		$n->set_global("cxxflags_$opts_ns", $cxxflags);
		$n->set_global("ldflags_$opts_ns", $ldflags);

		$cflags = "\$cflags_$opts_ns";
		$cxxflags = "\$cxxflags_$opts_ns";
		$ldflags = "\$ldflags_$opts_ns";
	}

	$objs = [];
	$names = [];
	foreach ($sources as $s) {
		$name_noext = preg_replace('#\\.[a-z]+$#i', '', basename($s));

		// ensure unique output name
		$i = 1;
		$new_name_noext = $name_noext;
		while (in_array($new_name_noext, $names)) {
			$new_name_noext = $name_noext . $i;
			$i++;
		}
		$name_noext = $new_name_noext;
		$names[] = $name_noext;

		$obj = $output . '.objs/'. $name_noext . $obj_ext;
		$depfile = $dep_path ? $dep_path($obj) : null;
		$obj_deps = [];
		if (array_key_exists($s, $obj_deps_map))
			$obj_deps = $obj_deps_map[$s];

		if (preg_match('#\\.(S|asm|s|as)$#', $s)) {
			$n->add_build('cc' . $suffix, [$obj], [$s], $obj_deps)
				->set_var('cflags', $cflags . ' -D__ASSEMBLY__');
		} else if (preg_match('#\\.c$#', $s)) {
			$b = $n->add_build('cc' . $suffix, [$obj], [$s], $obj_deps)
				->set_var('cflags', $cflags);
			if ($depfile)
				$b->set_var('depfile', $depfile);
		} else if (preg_match('#\\.(cpp|cc)$#', $s)) {
			$b = $n->add_build('cxx' . $suffix, [$obj], [$s], $obj_deps)
				->set_var('cxxflags', $cxxflags);
			if ($depfile)
				$b->set_var('depfile', $depfile);
		}

		$objs[] = $obj;
	}

	$n->add_build('link' . $suffix, [$output], $objs, $link_deps)
		->set_var('ldflags', $ldflags)
		->set_var('libs', $libs)
		->set_var('cflags', $cflags);

	return $output;
}

function add_command($name, $cmd, $deps = [])
{
	global $n;

	return $n->add_build('command', [$name], $deps)
		->set_var('cmd', $cmd)
		->set_var('desc', $name)
		->set_var('pool', 'console');
}

$n = new ninja();
$all_deps = [];

$topdir = realpath(dirname(__FILE__));

$n->set_global('aarch64_prefix', 'aarch64-linux-musl-');
if (file_exists('config.ini')) {
	$ini = new ini_file('config.ini');
	$n->set_global('aarch64_prefix', $ini->aarch64_prefix);
}

$n->set_global('topdir', '.');
$n->set_global('srcdir', '$topdir/src');
$n->set_global('ubootdir', '$topdir/src/uboot');
$n->set_global('builddir', '$topdir/.build');

$n->add_rule('copy', 'cp -T $in $out')
	->set_var('description', 'COPY $out');

$n->add_rule('command', '$cmd')
	->set_var('description', '$desc')
	->set_var('restat', '1');

$n->add_rule('configure', 'php -f $topdir/configure.php')
	->set_var('description', 'Reconfiguring')
	->set_var('generator', '1');

$n->add_rule('mkver', '$topdir/build-ver.sh $out')
	->set_var('description', 'MKVER $out')
	->set_var('restat', '1');

$n->add_rule('ln_s', 'ln -s $out')
	->set_var('description', 'LN_S $out')
	->set_var('restat', '1');

add_command('clean', 'ninja -t clean');

$n->set_global('linker_script', '$srcdir/p-boot.ld');
$n->set_global('startup_code', '$srcdir/start.S');

setup_gnu_toolchain('${aarch64_prefix}');
setup_gnu_toolchain('', 'native');

//$n->add_rule('linkscript', 'sed s/^0// $in > $out')
	//->set_var('description', 'LINKSCRIPT $out');

// tools

$mkboot_out = '$builddir/mksunxiboot';
add_cc_link_build([
	'name' => 'mkboot',
	'toolchain' => 'native',
	'output' => $mkboot_out,
	'sources' => ['$ubootdir/tools/mksunxiboot.c'],
	'cflags' => '',
	'ldflags' => '',
]);

$n->add_rule('addegon', $mkboot_out . ' --default-dt sd $in $out')
	->set_var('description', 'EGON $out');

$all_deps[] = add_cc_link_build([
	'name' => 'bconf_native',
	'toolchain' => 'native',
	'output' => '$builddir/p-boot-conf-native',
	'sources' => ['$srcdir/conf.c'],
	'cflags' => '',
	'ldflags' => '',
]);

$all_deps[] = add_cc_link_build([
	'name' => 'bconf',
	'output' => '$builddir/p-boot-conf',
	'sources' => ['$srcdir/conf.c'],
	'cflags' => '',
	'ldflags' => '-static -s',
]);

$all_deps[] = add_cc_link_build([
	'name' => 'bsel',
	'output' => '$builddir/p-boot-select',
	'sources' => ['$srcdir/bootsel.c'],
	'cflags' => '',
	'ldflags' => '-static -s',
]);

// p-boot

$configs = [
	'__KERNEL__',
	'__UBOOT__',
	'__ARM__',
	'__LINUX_ARM_ARCH__=8',
	'CONFIG_ARM64',
	'CONFIG_MACH_SUN50I',
	'CONFIG_SUNXI_GEN_SUN6I',
	'CONFIG_SPL_BUILD',
	'CONFIG_CONS_INDEX=1',
	'CONFIG_SUNXI_DE2',
//	'CONFIG_ARMV8_PSCI',
	'CONFIG_SUNXI_A64_TIMER_ERRATUM',
	'CONFIG_SYS_HZ=1000',
	'CONFIG_SUNXI_DRAM_DW',
	'CONFIG_SUNXI_DRAM_LPDDR3_STOCK',
	'CONFIG_SUNXI_DRAM_LPDDR3',
	'CONFIG_DRAM_CLK=552',
	'CONFIG_DRAM_ZQ=3881949',
	'CONFIG_NR_DRAM_BANKS=1',
	'CONFIG_SUNXI_DRAM_DW_32BIT',
	'CONFIG_SUNXI_DRAM_MAX_SIZE=0xC0000000',
	'CONFIG_DRAM_ODT_EN',
	'CONFIG_SYS_CLK_FREQ=816000000',
	'CONFIG_SYS_SDRAM_BASE=0x40000000',
	'CONFIG_SUNXI_SRAM_ADDRESS=0x10000',
	'CONFIG_SYS_CACHE_SHIFT_6',
	'CONFIG_SYS_CACHELINE_SIZE=64',
	'CONFIG_MMC_QUIRKS',
	'CONFIG_MMC2_BUS_WIDTH=8',
	'CONFIG_MMC_SUNXI_HAS_NEW_MODE',
	'CONFIG_MMC_HW_PARTITIONING',
	'CONFIG_ARCH_FIXUP_FDT_MEMORY',
//	'CONFIG_MMC_VERBOSE',
//	'CONFIG_MMC_TRACE',
	'FDT_ASSUME_MASK=0xff',
	'CONFIG_ATF_TO_LINUX',
];

$cflags = [
	array_map(function($i) {
		return '-D' . $i;
	}, $configs),

	// includes
	'-include linux/kconfig.h',
	'-I$builddir',
	'-I$srcdir',
	'-I$ubootdir/include',
	'-I$ubootdir/include/asm-generic',
	'-I$ubootdir/arch/arm/include',
	'-I$ubootdir/arch/arm/include/asm',
	'-I$ubootdir/arch/arm/include/asm/proc-armv',
	'-I$ubootdir/arch/arm/include/asm/armv8',
	'-I$ubootdir/arch/arm/include/asm/arch-sunxi',
	'-I$ubootdir/scripts/dtc/libfdt',
	'-I$ubootdir/lib/libfdt',

	// warnings
	'-Wall',
	'-Wstrict-prototypes',
	'-Wno-format-security',
	'-Wno-format-nonliteral',
	'-Werror=date-time',
	'-Wno-unused-function',

	// build flags
	'-fno-builtin',
	'-ffreestanding',
	'-fshort-wchar',
	'-fno-strict-aliasing',
	'-fno-PIE',
	'-fno-stack-protector',
	'-fno-delete-null-pointer-checks',
	'-fno-pic',
	'-mstrict-align',
	'-fno-common',
	'-ffixed-r9',
	'-ffixed-x18',
	'-march=armv8-a',
	'-Os',
	'-g0',
	'-ffunction-sections',
	'-fdata-sections',
	//'-fstack-usage',
	//'-fmacro-prefix-map=/workspace/megous.com/orangepi-pc/u-boot-v2020.01/=',
	//'-fomit-frame-pointer',
	//'-fno-exceptions',
	//'-fno-asynchronous-unwind-tables',
	//'-fno-unwind-tables',
];

$ldflags = [
	'-T$linker_script',
	'-static',
	'-Wl,--gc-sections',
	'-Wl,--fix-cortex-a53-843419',
	'-nostdlib'
];

$n->set_global('pboot_cflags', implode(' ', flat($cflags)));
$n->set_global('pboot_ldflags', implode(' ', flat($ldflags)));

function p_boot($conf) {
	global $all_deps, $mkboot_out, $n;

	$name = $conf['name'] ?? null;
	if (!$name)
		die('Missing p_boot variant name');

	$elf_out = "\$builddir/$name/p-boot.elf";
	add_cc_link_build([
		'name' => str_replace('-', '_', $name),
		'output' => $elf_out,
		'sources' => [
			'$srcdir/start.S',
			'$srcdir/main.c',
			'$srcdir/debug.c',
			'$srcdir/lib.c',
			'$srcdir/pmic.c',
			'$srcdir/mmu.c',
			'$srcdir/lradc.c',

			'$ubootdir/arch/arm/cpu/armv8/cache.S',
			'$ubootdir/arch/arm/cpu/armv8/tlb.S',
			'$ubootdir/arch/arm/cpu/armv8/transition.S',
			'$ubootdir/arch/arm/cpu/armv8/cache_v8.c',
			'$ubootdir/arch/arm/cpu/armv8/generic_timer.c',
			'$ubootdir/arch/arm/lib/cache.c',
			'$ubootdir/arch/arm/mach-sunxi/clock_sun6i.c',
			'$ubootdir/arch/arm/mach-sunxi/dram_helpers.c',
			'$ubootdir/arch/arm/mach-sunxi/dram_sunxi_dw.c',
			'$ubootdir/arch/arm/mach-sunxi/pinmux.c',
			'$ubootdir/arch/arm/mach-sunxi/dram_timings/lpddr3_stock.c',
			'$ubootdir/lib/libfdt/fdt.c',
			'$ubootdir/lib/libfdt/fdt_addresses.c',
			'$ubootdir/lib/libfdt/fdt_empty_tree.c',
			'$ubootdir/lib/libfdt/fdt_rw.c',
			'$ubootdir/lib/libfdt/fdt_strerror.c',
			'$ubootdir/lib/libfdt/fdt_sw.c',
			'$ubootdir/lib/libfdt/fdt_wip.c',
			'$ubootdir/lib/libfdt/fdt_region.c',
			'$ubootdir/lib/libfdt/fdt_ro.c',
			'$ubootdir/drivers/gpio/sunxi_gpio.c',
			'$ubootdir/drivers/mmc/mmc.c',
			'$ubootdir/drivers/mmc/sunxi_mmc.c',
			'$ubootdir/common/fdt_support.c',
			'$ubootdir/lib/time.c',
		],
		'obj_deps' => [
			'$srcdir/main.c' => '$builddir/build-ver.h',
		],
		'cflags' => implode(' ', flat($conf['cflags'])),
		'ldflags' => implode(' ', flat($conf['ldflags'])),
		'link_deps' => ['$linker_script'],
	]);

	$bin_out = "\$builddir/$name/p-boot-bare.bin";
	$n->add_build('elf2bin', [$bin_out], [$elf_out]);

	$bin_egon_out = "\$builddir/$name.bin";
	$n->add_build('addegon', [$bin_egon_out], [$bin_out], [$mkboot_out]);
	$all_deps[] = $bin_egon_out;

	$as_out = "\$builddir/$name/p-boot.as";
	$n->add_build('elf2as', [$as_out], [$elf_out]);
	$all_deps[] = $as_out;

	$size_out = "\$builddir/$name/p-boot.size";
	$n->add_build('size', [$size_out], [$elf_out]);
	$all_deps[] = $size_out;
}

function p_boot_norm_lto($conf)
{
	p_boot([
		'name' => $conf['name'],
		'cflags' => $conf['cflags'],
		'ldflags' => $conf['ldflags'],
	]);

	p_boot([
		'name' => $conf['name'] . '-lto',
		'cflags' => [$conf['cflags'], '-flto'],
		'ldflags' => [$conf['ldflags'], '-flto'],
	]);
}

p_boot_norm_lto([
	'name' => 'p-boot-debug',
	'cflags' => [
		'$pboot_cflags',
		'-DSERIAL_CONSOLE',
		'-DDEBUG',
	],
	'ldflags' => ['$pboot_ldflags'],
]);

p_boot_norm_lto([
	'name' => 'p-boot',
	'cflags' => [
		'$pboot_cflags',
		 '-DSERIAL_CONSOLE'
	],
	'ldflags' => ['$pboot_ldflags'],
]);

p_boot_norm_lto([
	'name' => 'p-boot-silent',
	'cflags' => [
		'$pboot_cflags',
	],
	'ldflags' => ['$pboot_ldflags'],
]);

$n->add_build('mkver', ['$builddir/build-ver.h'], ['always']);

$n->default = 'all';
$n->add_build('phony', ['always']);
$n->add_build('phony', ['all'], $all_deps);

$pn = clone $n;

$n->add_build('configure', ['build.ninja'], ['$topdir/configure.php'])
	->set_var('pool', 'console');

$n->write($topdir . "/build.ninja");

@mkdir("build");
$pn->set_global('aarch64_prefix', 'aarch64-linux-musl-');
$pn->set_global('builddir', '.');
$pn->set_global('topdir', '..');
$pn->write($topdir . "/build/build.ninja");
