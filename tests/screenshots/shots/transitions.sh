# shellcheck shell=sh

# partway through opening Game Boy
for style in slide fade slide_fade; do
	begin
	on transitions
	cfg transitions "style=$style" speed=slow
	press "down down a"
	after 200
	shot "style-$(echo "$style" | tr _ -)"
done
