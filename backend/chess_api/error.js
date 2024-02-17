function buildErrorResponse(errorCode, res) {
    if (typeof errorCode === "string") errorCode = parseInt(errorCode);

    switch (errorCode) {
        case 10:
            res.status(400).send("Public Token does not exist");
            break;
        case 11:
            res.status(400).send("Private Token does not exist");
            break;
        case 12:
            res.status(400).send("The given Private and Public Token belong to the same Person");
            break;
        case 13:
            res.status(400).send("The Username contains special letters - Please use [A-Za-z0-9]");
            break;
        case 14:
            res.status(409).send("The User already has this Name");
            break;
        case 15:
            res.status(409).send("The User is already no Friend");
            break;
        case 16:
            res.status(409).send("The User is already a Friend");
            break;
        case 20:
            res.status(500).send(); //DataBase Error
            break;
        default:
            res.status(500).send(); //Unknown Error
            break;
    }
}

module.exports = {
    er_buildErrorResponse: buildErrorResponse
};