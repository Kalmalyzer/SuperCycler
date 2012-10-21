
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

Default "TestIffParser"
Default "TestIlbmParser"
