int animation_pos = -1;
char animation_str[] = "\\|/-";
void animation_init(void)
{
	animation_pos = -1;
}
void animation_step(void)
{
	if (animation_pos != -1)
		fprintf(stderr, "\b");
	else
		animation_pos = 0;

	fprintf(stderr, "%c", animation_str[animation_pos]);
	if (++animation_pos >= strlen(animation_str))
		animation_pos = 0;
}

	if (access(TTY_DEV, F_OK)) {
		fprintf(stderr, "waiting for %s ", TTY_DEV);
		animation_init();
		while (access(TTY_DEV, F_OK)) {
			animation_step();
			usleep(200000);
		}
		fprintf(stderr, "\bOK\n");
	}
