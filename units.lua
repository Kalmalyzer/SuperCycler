
Program {
	Name = "TestIffParser",
	Sources = {
		"parseIff.c",
		"TestIffParser.c",
	},
}

Program {
	Name = "TestIlbmParser",
	Sources = {
		"parseIff.c",
		"parseIlbm.c",
		"TestIlbmParser.c",
	},
}

Program {
	Name = "displayIlbm",
	Sources = {
		"parseIff.c",
		"parseIlbm.c",
		"displayIlbm.c",
	},
}

Default "TestIffParser"
Default "TestIlbmParser"
Default "displayIlbm"
