#diainp

void DialogueInputUpdate()
{
	switch(commandCode)
	{
		case CMD_NONE:
			break;
		case CMD_STATUS:
			intent = INTENT_STATUS;
			commandCode = CMD_NONE;
			break;
		case CMD_HELP:
			intent = INTENT_HELP;
			commandCode = CMD_NONE;
			break;
		case CMD_UPTIME:
			intent = INTENT_UPTIME;
			commandCode = CMD_NONE;
			break;
		case CMD_QUIT:
			intent = INTENT_QUIT;
			commandCode = CMD_NONE;
			break;
		case CMD_PLAYX:
			intent = INTENT_PHONEME;
			commandCode = CMD_NONE;
			break;
		default:
			break;
	}
}

void main()
{
	DialogueInputUpdate();
}