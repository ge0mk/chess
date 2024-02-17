const { db_addUser, db_addFriend, db_changeUserName, db_removeFriend, db_getFriends} = require('./db-functions.js');
const { er_buildErrorResponse } = require('./error.js')

const express = require("express");
const bodyParser = require("body-parser");
const app = express();
const port = 3000;

app.use(bodyParser.json());

app.get("/", (req, res) => {
res.send("Hello World!");
});

app.post("/api/addUser", async (req, res) => {
    const receivedString = req.body.userName;

    try {
        const result = await db_addUser(receivedString);
        res.json({
            userName: result.name, publicToken: result.publicToken,
            privateToken: result.privateToken
        });
    } catch (error) {
        er_buildErrorResponse(error.message, res);
    }

});

app.post("/api/addFriend", async (req, res) => {
    const privateToken = req.body.privateToken;
    const publicTokenOfFriend = req.body.publicTokenOfFriend;

    try {
        await db_addFriend(privateToken, publicTokenOfFriend);
        res.sendStatus(201);

    } catch (error) {
        er_buildErrorResponse(error.message, res);
    }
});

app.post("/api/changeUserName", async (req, res) => {
    const privateToken = req.body.privateToken;
    const newUserName = req.body.userName;

    try {
        await db_changeUserName(privateToken, newUserName);
        res.sendStatus(201);

    } catch (error) {
        er_buildErrorResponse(error.message, res);
    }
});

app.post("/api/removeFriend", async (req, res) => {
    const privateToken = req.body.privateToken;
    const publicTokenOfFriend = req.body.publicTokenOfFriend;

    try {
        await db_removeFriend(privateToken, publicTokenOfFriend);
        res.sendStatus(201);

    } catch (error) {
        er_buildErrorResponse(error.message, res);
    }
});

app.post("/api/getFriends", async (req, res) => {
    const privateToken = req.body.privateToken;

    try {
        const result = await db_getFriends(privateToken);
        res.json(result);
    } catch (error) {
        er_buildErrorResponse(error.message, res);
    }

});

app.listen(port, () => {
console.log(`Server listening on port ${port}`);
});

