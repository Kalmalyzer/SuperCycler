
Program {
	Name = "TestIffParser",
	Sources = {
		"parseIff.c",
		"TestIffParser.c",
	},
}

Program {
	Name = "TestIffImageLoader",
	Sources = {
		"parseIff.c",
		"Ilbm.c",
		"TestIffImageLoader.c",
	},
}

Program {
	Name = "SuperCycler",
	Sources = {
		"parseIff.c",
		"Ilbm.c",
		"ScreenAndInput.c",
		"SuperCycler.c",
	},
}

Default "TestIffParser"
Default "TestIffImageLoader"
Default "SuperCycler"
