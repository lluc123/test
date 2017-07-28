b Org_Load
command
	fin
	printf "\n\nEND_LOAD\n\n"
	print head.tempo
	print head.ins[0]
	print head.ins[1]
	print head.ins[2]
	print head.ins[3]
	print head.ins[4]
	print head.ins[5]
	print head.ins[6]
	print head.ins[7]
	print head.ins[8]
	print head.ins[9]
	print head.ins[10]
	print head.ins[11]
	print head.ins[12]
	print head.ins[13]
	print head.ins[14]
	print head.ins[15]
	c
end

b 284
command
printf "\n\nBefore_loop\n\n"
print samples_per_millisecond
print samples_per_beat
end

set args Cave_Story.org
run
